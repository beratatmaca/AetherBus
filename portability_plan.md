# Implementation Plan: Pimpl-Based Cross-Platform PtyProxy

To support Linux, macOS, and Windows cleanly without changing the public APIs or breaking existing instantiation points (such as `PtyProxy proxy;` in `SessionWidget` and tests), we will use the **Private Implementation (Pimpl) pattern**.

The platform-specific logic will be moved to separate implementation files inside OS-specific folders:
- `src/core/linux/linux_pty_proxy.cpp`
- `src/core/mac/mac_pty_proxy.cpp`
- `src/core/win/win_pty_proxy.cpp`

---

## 1. Directory Structure
```
src/core/
  ├── pty_proxy.h            # Public header (Pimpl definition)
  ├── pty_proxy.cpp          # Public delegation and OS-dispatch factory
  ├── pty_proxy_impl.h       # Shared base implementation interface
  ├── linux/
  │     └── linux_pty_proxy.cpp  # Linux PTY and termios2 implementation
  ├── mac/
  │     └── mac_pty_proxy.cpp    # macOS BSD openpty stub
  └── win/
        └── win_pty_proxy.cpp    # Windows named pipe / bridge stub
```

---

## 2. Shared Implementations Interface (`pty_proxy_impl.h`)
`PtyProxyImpl` is an abstract interface that each OS-specific proxy must implement. It has a pointer back to the public `PtyProxy` to emit signals:

```cpp
#pragma once
#include "core/pty_proxy.h"

namespace aether {

class PtyProxyImpl {
public:
    explicit PtyProxyImpl(PtyProxy *q) : q_ptr(q) {}
    virtual ~PtyProxyImpl() = default;

    virtual bool open(const SerialConfig &config) = 0;
    virtual void close() = 0;
    virtual bool isRunning() const = 0;
    virtual QString slavePath() const = 0;
    virtual bool injectToDevice(const QByteArray &bytes) = 0;
    virtual bool injectToApp(const QByteArray &bytes) = 0;

    virtual PtyProxy::ModemLines modemLines() const = 0;
    virtual bool setRts(bool on) = 0;
    virtual bool setDtr(bool on) = 0;
    virtual bool sendBreak() = 0;

    virtual bool startCapture(const QString &path) = 0;
    virtual void stopCapture() = 0;
    virtual bool isCapturing() const = 0;

    virtual PtyProxy::Stats stats() const = 0;

protected:
    PtyProxy *q_ptr; // for emitting signals
};

} // namespace aether
```

---

## 3. Delegation in `pty_proxy.cpp`
`PtyProxy` delegates all public operations to its private implementation `d`. In its constructor, it instantiates the correct subclass depending on compile-time OS flags:

```cpp
#include "core/pty_proxy.h"
#include "core/pty_proxy_impl.h"

#if defined(Q_OS_WIN)
#include "core/win/win_pty_proxy.h" // Windows implementation subclass
#elif defined(Q_OS_MAC)
#include "core/mac/mac_pty_proxy.h" // macOS implementation subclass
#else
#include "core/linux/linux_pty_proxy.h" // Linux implementation subclass
#endif

namespace aether {

PtyProxy::PtyProxy(QObject *parent) : QObject(parent) {
#if defined(Q_OS_WIN)
    d = std::make_unique<WindowsPtyProxy>(this);
#elif defined(Q_OS_MAC)
    d = std::make_unique<MacPtyProxy>(this);
#else
    d = std::make_unique<LinuxPtyProxy>(this);
#endif
}

bool PtyProxy::open(const SerialConfig &config) { return d->open(config); }
// ... other delegation wrappers ...
}
```

---

## 4. Verification
- We will refactor the codebase to implement this structure.
- We will copy the existing Linux implementation of `PtyProxy` into `src/core/linux/linux_pty_proxy.cpp`.
- We will write placeholder/stub classes for macOS and Windows.
- Build and run tests on Linux to verify 100% regression-free behavior.
