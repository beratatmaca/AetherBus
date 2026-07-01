/**
 * @file signal_cleanup.hpp
 * @brief Crash- and signal-safe release of proxy descriptors and symlinks.
 *
 * A running @ref aether::PtyProxy may create a symlink to its slave PTY node.
 * On a normal shutdown the proxy unlinks it, but on a sudden exit
 * (SIGINT/SIGTERM/crash) the process dies before that runs, leaving a dangling
 * symlink on disk (the kernel reclaims the file descriptors, but not the
 * symlink). This module keeps a process-global, async-signal-safe registry of
 * active resources and installs handlers that unlink symlinks and close
 * descriptors before the process terminates.
 *
 * The emergency handler touches only async-signal-safe calls (@c unlink,
 * @c close, @c signal, @c raise) — no heap, Qt, or locking.
 */
#pragma once

namespace aether {

/**
 * @brief Register a proxy's cleanup resources for signal-time release.
 *
 * The symlink path is copied into an internal fixed buffer, so the caller need
 * not keep it alive. Safe to call from the main thread.
 *
 * @param symlinkPath NUL-terminated path to unlink on exit, or @c nullptr/empty.
 * @param uartFd      Physical UART descriptor to close, or negative for none.
 * @param masterFd    PTY master descriptor to close, or negative for none.
 * @return A slot handle to pass to @ref releaseCleanup, or -1 if the registry
 *         is full (cleanup simply won't be tracked for this proxy).
 */
int registerCleanup(const char *symlinkPath, int uartFd, int masterFd);

/**
 * @brief Release a slot previously obtained from @ref registerCleanup.
 * @param slot The handle returned by @ref registerCleanup (a no-op if negative).
 */
void releaseCleanup(int slot);

/**
 * @brief Unlink symlinks and close descriptors for every active slot.
 *
 * Async-signal-safe. Invoked by the installed signal handlers; also callable
 * directly (e.g. from tests) as it does not raise or terminate.
 */
void runEmergencyCleanup();

/**
 * @brief Install signal handlers that run @ref runEmergencyCleanup on exit.
 *
 * Idempotent. Handles termination (SIGINT/SIGTERM/SIGHUP/SIGQUIT) and fatal
 * (SIGSEGV/SIGABRT/SIGBUS/SIGFPE) signals: after cleanup the default
 * disposition is restored and the signal re-raised, so termination and core
 * dumps proceed normally. A no-op on non-POSIX platforms.
 */
void installSignalHandlers();

}  // namespace aether
