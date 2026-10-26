#pragma once

namespace mobile_system {

// Installs handlers that append signal, fault address, thread name, device
// details and a best-effort native backtrace to the crash log before letting
// the default action produce a tombstone.  Call once from the native main
// thread; the handler itself is process-wide.
void installCrashReporter();

}  // namespace mobile_system
