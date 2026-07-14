#include "core/common/signal_cleanup.hpp"

#if defined(__unix__) || defined(__APPLE__)

#include <array>
#include <atomic>
#include <climits>
#include <csignal>
#include <cstring>
#include <mutex>

#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace aether {

namespace {

constexpr int kMaxSlots = 32;  ///< Upper bound on concurrently tracked proxies (one per session tab, plus slack).

struct CleanupSlot {
    std::atomic<bool> active{false};
    std::atomic<bool> hasSymlink{false};
    std::atomic<int> uartFd{-1};
    std::atomic<int> masterFd{-1};
    std::array<char, PATH_MAX> symlink{};
};

std::array<CleanupSlot, kMaxSlots> g_slots;  ///< Static storage: never freed, safe to touch from a signal handler.
std::mutex g_registryMutex;                  ///< guards slot acquisition; NOT taken in the handler.

// Signals we clean up after: termination requests first, then fatal faults.
constexpr std::array<int, 8> kSignals{SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGSEGV, SIGABRT, SIGBUS, SIGFPE};

extern "C" void handleFatalSignal(int sig) {
    runEmergencyCleanup();
    // Restore default disposition and re-raise so the process terminates (or
    // dumps core) with the semantics the signal would normally have.
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

}  // namespace

int registerCleanup(const char *symlinkPath, int uartFd, int masterFd) {
    std::lock_guard<std::mutex> lk(g_registryMutex);
    for (int i = 0; i < kMaxSlots; ++i) {
        CleanupSlot &slot = g_slots[i];
        if (slot.active.load(std::memory_order_relaxed)) {
            continue;
        }
        slot.hasSymlink.store(false, std::memory_order_relaxed);
        if (symlinkPath != nullptr && symlinkPath[0] != '\0') {
            std::strncpy(slot.symlink.data(), symlinkPath, PATH_MAX - 1);
            slot.symlink[PATH_MAX - 1] = '\0';
            slot.hasSymlink.store(true, std::memory_order_relaxed);
        }
        slot.uartFd.store(uartFd, std::memory_order_relaxed);
        slot.masterFd.store(masterFd, std::memory_order_relaxed);
        // Publish last: the handler only reads a slot once active is visible.
        slot.active.store(true, std::memory_order_release);
        return i;
    }
    return -1;
}

void releaseCleanup(int slot) {
    if (slot < 0 || slot >= kMaxSlots) {
        return;
    }
    g_slots[slot].active.store(false, std::memory_order_release);
}

void runEmergencyCleanup() {
    for (auto &slot : g_slots) {
        if (!slot.active.load(std::memory_order_acquire)) {
            continue;
        }
        if (slot.hasSymlink.load(std::memory_order_relaxed)) {
            ::unlink(slot.symlink.data());
        }
        const int uartFd = slot.uartFd.load(std::memory_order_relaxed);
        if (uartFd >= 0) {
            ::close(uartFd);
        }
        const int masterFd = slot.masterFd.load(std::memory_order_relaxed);
        if (masterFd >= 0) {
            ::close(masterFd);
        }
    }
}

void installSignalHandlers() {
    static std::atomic<bool> installed{false};
    bool expected = false;
    if (!installed.compare_exchange_strong(expected, true)) {
        return;  // already installed
    }
    struct sigaction sa {};
    sa.sa_handler = &handleFatalSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    for (const int sig : kSignals) {
        ::sigaction(sig, &sa, nullptr);
    }
}

}  // namespace aether

#else  // Non-POSIX: no PTY proxy, so nothing to clean up.

namespace aether {
int registerCleanup(const char *, int, int) {
    return -1;
}
void releaseCleanup(int) {}
void runEmergencyCleanup() {}
void installSignalHandlers() {}
}  // namespace aether

#endif
