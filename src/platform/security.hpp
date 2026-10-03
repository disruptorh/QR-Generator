#ifndef QR_PLATFORM_SECURITY_HPP_
#define QR_PLATFORM_SECURITY_HPP_

#include <cstdint>
#include <string>

#include "core/result.hpp"

namespace platform {

// What the sandbox did, so the UI can state it plainly instead of implying a
// guarantee the app cannot actually make.
struct SandboxStatus {
  bool core_dumps_disabled = false;   // RLIMIT_CORE set to 0
  bool network_blocked = false;       // seccomp denies socket syscalls
  bool seccomp_unavailable = false;   // kernel or prctl refused; network is not blocked
};

const char* sandbox_summary(const SandboxStatus& status);

// Defence in depth for a program that must never talk to the network and should
// not leave credentials in a core file.
//
// The seccomp filter is installed with prctl(PR_SET_SECCOMP) and a hand-assembled
// BPF program, because the build machine has no libseccomp and no seccomp.h. Only
// the syscall numbers are needed, so nothing is lost.
//
// The filter is installed as early as possible, before any window or file work.
// If the kernel refuses it the app says so and keeps running: a GUI that dies on
// startup would be worse than one that reports it could not lock itself down.
//
// Note the boundary honestly: this stops socket() and friends, so the app cannot
// open a connection. It is not a substitute for an AppArmor or firejail profile,
// which also constrains the filesystem.
core::Result<SandboxStatus> harden_process();

// Installed automatically by harden_process(); exposed so main() can report it on
// a non-default order if needed.
const SandboxStatus& sandbox_status();

}  // namespace platform

#endif  // QR_PLATFORM_SECURITY_HPP_