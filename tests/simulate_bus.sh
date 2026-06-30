#!/usr/bin/env bash

# Exit cleanup helper
cleanup() {
    echo -e "\nCleaning up background jobs..."
    kill "$SOCAT_PID" 2>/dev/null
    kill "$AGENT_A_PID" 2>/dev/null
    kill "$AGENT_B_PID" 2>/dev/null
    rm -f ./tty_sniffed
    exit 0
}
trap cleanup SIGINT SIGTERM EXIT

# Check if socat is installed
if ! command -v socat &>/dev/null; then
    echo "Error: 'socat' utility is required but not installed."
    echo "Please install it via: sudo apt install socat"
    exit 1
fi

echo "============================================================="
# Start socat to create virtual linked PTY pair (PTS_A <-> PTS_B)
# PTS_A represents the physical port from AetherBus's perspective
# PTS_B represents the backend device connection
socat -d -d pty,raw,echo=0 pty,raw,echo=0 2>socat.log &
SOCAT_PID=$!

# Wait for ports to be created and read them from the log
sleep 1
PTS_A=$(grep -o "/dev/pts/[0-9]*" socat.log | head -n 1)
PTS_B=$(grep -o "/dev/pts/[0-9]*" socat.log | tail -n 1)

if [ -z "$PTS_A" ] || [ -z "$PTS_B" ]; then
    echo "Error: Failed to allocate PTY ports via socat. See socat.log."
    cleanup
fi

# Configure ports to standard raw 115200 8N1 serial attributes
stty -F "$PTS_A" raw -echo cs8 115200
stty -F "$PTS_B" raw -echo cs8 115200

echo "============================================================="
echo "                AetherBus Sniffing Simulator (Bash)          "
echo "============================================================="
echo "1. Physical Port (PTS_A): $PTS_A"
echo "2. Device Mock Port (PTS_B): $PTS_B"
echo "3. Symlink to be created by GUI: ./tty_sniffed"
echo "-------------------------------------------------------------"
echo "INSTRUCTIONS FOR AETHERBUS GUI:"
echo "  - Physical device: Copy and paste: $PTS_A"
echo "  - Slave symlink:   Copy and paste: ./tty_sniffed"
echo "  - Click 'Start Interception' to begin sniffing."
echo "============================================================="

# Agent A (Device): Simulates physical device on PTS_B sending telemetry
# Telemetry frame: 55 AA 01 02 03 0D 0A
(
    while true; do
        # Send telemetry frame in binary format
        echo -ne "\x55\xaa\x01\x02\x03\x0d\x0a" > "$PTS_B"
        echo "[Agent A (Device)] Sent Telemetry: 55AA0102030D0A"
        sleep 2
    done
) &
AGENT_A_PID=$!

# Agent A listener: Log commands from PTS_B
(
    hexdump -v -e '1/1 "%02X "' "$PTS_B" | while read -r line; do
        if [ ! -z "$line" ]; then
            echo "[Agent A (Device)] Received Command: $line"
        fi
    done
) &
AGENT_A_LISTENER_PID=$!

# Agent B (App): Wait for AetherBus to create the symlink, then connect
(
    echo "[Agent B (App)] Waiting for ./tty_sniffed to be created by AetherBus..."
    while [ ! -e "./tty_sniffed" ]; do
        sleep 0.5
    done
    echo "[Agent B (App)] Symlink detected! Connecting to ./tty_sniffed..."
    
    # Configure terminal line settings
    stty -F ./tty_sniffed raw -echo cs8 115200 2>/dev/null

    # Command frame to reply: AA 55 FF 00 0D 0A
    # Read incoming bytes and reply
    hexdump -v -e '1/1 "%02X "' ./tty_sniffed | while read -r line; do
        if [ ! -z "$line" ]; then
            echo "[Agent B (App)] Received Telemetry: $line"
            # Processing delay, then reply
            sleep 0.1
            echo -ne "\xaa\x55\xff\x00\x0d\x0a" > ./tty_sniffed
            echo "[Agent B (App)] Sent Command Reply: AA55FF000D0A"
        fi
    done
) &
AGENT_B_PID=$!

# Keep running
wait
