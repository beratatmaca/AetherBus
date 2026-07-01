#!/usr/bin/env bash

# Exit cleanup helper
cleanup() {
    echo -e "\nCleaning up background jobs..."
    kill "$SOCAT_PID" 2>/dev/null
    kill "$AGENT_A_PID" 2>/dev/null
    kill "$AGENT_B_PID" 2>/dev/null
    kill "$AGENT_C_SENDER_PID" 2>/dev/null
    kill "$AGENT_C_LISTENER_PID" 2>/dev/null
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

# Setup CAN interface (vcan0)
echo "============================================================="
echo "Initializing virtual CAN interface (vcan0)..."
if ! ip link show vcan0 &>/dev/null; then
    echo "vcan0 interface not found. Attempting to create it (requires sudo)..."
    sudo modprobe vcan 2>/dev/null || true
    if sudo ip link add dev vcan0 type vcan 2>/dev/null; then
        echo "vcan0 created successfully."
    else
        echo "WARNING: Failed to create vcan0. If it fails, please run:"
        echo "  sudo modprobe vcan"
        echo "  sudo ip link add dev vcan0 type vcan"
        echo "  sudo ip link set up vcan0"
    fi
fi

if ip link show vcan0 &>/dev/null; then
    if ! ip link show vcan0 | grep -q "UP"; then
        echo "Bringing vcan0 interface UP..."
        if sudo ip link set up vcan0 2>/dev/null; then
            echo "vcan0 is now UP."
        else
            echo "WARNING: Failed to bring vcan0 UP. Please run: sudo ip link set up vcan0"
        fi
    else
        echo "vcan0 is already UP."
    fi
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
echo "                AetherBus Sniffing Simulator                 "
echo "============================================================="
echo "1. Physical Port (PTS_A): $PTS_A"
echo "2. Device Mock Port (PTS_B): $PTS_B"
echo "3. Symlink to be created by GUI: ./tty_sniffed"
echo "4. CAN Interface: vcan0"
echo "-------------------------------------------------------------"
echo "INSTRUCTIONS FOR AETHERBUS GUI:"
echo "  - For Serial Interception:"
echo "    - Physical device: Copy and paste: $PTS_A"
echo "    - Slave symlink:   Copy and paste: ./tty_sniffed"
echo "    - Click 'Start Interception' to begin sniffing."
echo "  - For CAN Interception:"
echo "    - Select interface: vcan0"
echo "============================================================="

# Agent A (Serial - Device Mock): Simulates physical device on PTS_B
(
    # Open file descriptor 3 for reading and writing to hold PTY open
    exec 3<> "$PTS_B"
    
    # Start sender in background using the descriptor
    (
        while true; do
            echo -ne "Hello from Agent A\r\n" >&3
            sleep 3
        done
    ) &
    AGENT_A_SENDER_INNER_PID=$!
    
    # Reader loop using the descriptor
    while read -r -u 3 line; do
        clean_line=$(echo "$line" | tr -d '\r')
        echo "[Agent A (Serial Device)] Received: $clean_line"
    done
    
    kill "$AGENT_A_SENDER_INNER_PID" 2>/dev/null
) &
AGENT_A_PID=$!

# Agent B (Serial - App Mock): Connects to ./tty_sniffed once GUI creates it
(
    while [ ! -e "./tty_sniffed" ]; do
        sleep 0.5
    done
    
    # Configure terminal line settings
    stty -F ./tty_sniffed raw -echo cs8 115200 2>/dev/null
    
    # Open file descriptor 3 for reading and writing to hold PTY open
    exec 3<> ./tty_sniffed
    
    # Start sender in background using the descriptor
    (
        while true; do
            echo -ne "Hello from Agent B\r\n" >&3
            sleep 3
        done
    ) &
    AGENT_B_SENDER_INNER_PID=$!
    
    # Reader loop using the descriptor
    while read -r -u 3 line; do
        clean_line=$(echo "$line" | tr -d '\r')
        echo "[Agent B (Serial App)] Received: $clean_line"
    done
    
    kill "$AGENT_B_SENDER_INNER_PID" 2>/dev/null
) &
AGENT_B_PID=$!

# Agent C (CAN Mock): Sends random CAN messages to vcan0 and listens for replies
# Sender loop
(
    while true; do
        if command -v cansend &>/dev/null && ip link show vcan0 | grep -q "UP" 2>/dev/null; then
            # Random CAN ID between 0x100 and 0x7FF
            CAN_ID=$(printf "%X" $((0x100 + RANDOM % 0x700)))
            # Random length between 1 and 8 bytes
            LEN=$((1 + RANDOM % 8))
            DATA=""
            for ((i=0; i<LEN; i++)); do
                DATA+=$(printf "%02X" $((RANDOM % 256)))
            done
            cansend vcan0 "${CAN_ID}#${DATA}" 2>/dev/null
        fi
        sleep 2
    done
) &
AGENT_C_SENDER_PID=$!

# Listener loop
(
    if command -v candump &>/dev/null; then
        candump vcan0 2>/dev/null | while read -r line; do
            echo "[Agent C (CAN)] Received: $line"
        done
    fi
) &
AGENT_C_LISTENER_PID=$!

# Keep running
wait
