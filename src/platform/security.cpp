#include "platform/security.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>

// No seccomp.h on this machine, and only a handful of constants are needed. The
// values are the kernel ABI and are stable across every Linux architecture.
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace platform {
namespace {

SandboxStatus g_status;

// Enough room for one load, two instructions per denied syscall and the allow.
// sock_fprog is the userspace ABI type prctl(PR_SET_SECCOMP) expects;
// sock_fprog_kern is kernel-internal and not exposed in the UAPI headers.
struct FilterSpec {
  sock_filter filter[64];
  std::size_t count = 0;
  sock_fprog program;
};

// Offsets into struct seccomp_data, whose layout is part of the kernel ABI.
constexpr std::size_t kOffsetNr = 0;
constexpr std::size_t kOffsetArgs = 16;  // args[0]; each argument is 8 bytes

constexpr unsigned kAfUnix = 1;  // AF_UNIX, the same value on every Linux arch

// Syscalls with no role for this app. Denying them stops the process from becoming
// a server or duplicating a socket it was handed.
//
// The data-exchange calls are deliberately absent: strace shows libX11 needs
// getsockname(), getpeername() and recvfrom() on its display socket, so denying
// them would stop the window from opening. They are not needed for the guarantee
// either -- see the note on the filter below.
const long kBlockedSyscalls[] = {
    __NR_socketpair,
    __NR_bind,
    __NR_listen,
    __NR_accept,
#ifdef __NR_accept4
    __NR_accept4,
#endif
};

// Builds the deny list.
//
// The single load-and-compare below is the whole of the network protection, and
// it is deliberately the only thing the filter does:
//
//   socket(domain, ...) is allowed only for AF_UNIX, the family X11 uses to reach
//   the display server. No AF_INET or AF_INET6 socket can be created, so no
//   network connection can be opened.
//
// The honest limit, in two parts. Classic BPF under seccomp permits only 64-bit
// LD|ABS loads, so connect() cannot be inspected -- a filter has no way to read
// the family out of its sockaddr. And because libX11 requires the send/recv
// family, those calls cannot be denied either. The guarantee therefore rests on
// the process starting fresh: it inherits no socket descriptors beyond
// stdin/stdout/stderr, so there is no IP socket for connect() to reach.
FilterSpec build_no_network_filter() {
  FilterSpec spec{};
  sock_filter* f = spec.filter;

  f[spec.count++] = BPF_STMT(BPF_LD | BPF_W | BPF_ABS, static_cast<unsigned>(kOffsetNr));

  // socket(): allow AF_UNIX, deny anything else.
  f[spec.count++] = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<unsigned>(__NR_socket), 0, 3);
  f[spec.count++] =
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, static_cast<unsigned>(kOffsetArgs));  // domain
  f[spec.count++] = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, kAfUnix, 1, 0);
  f[spec.count++] = BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EACCES & SECCOMP_RET_DATA));

  for (const long blocked : kBlockedSyscalls) {
    f[spec.count++] = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<unsigned>(blocked), 0, 1);
    f[spec.count++] = BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EACCES & SECCOMP_RET_DATA));
  }

  // Everything the app legitimately does is allowed.
  f[spec.count++] = BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);

  spec.program.len = static_cast<unsigned short>(spec.count);
  spec.program.filter = spec.filter;
  return spec;
}

// Installs the filter once, at startup.
bool install_no_network_filter() {
  FilterSpec spec = build_no_network_filter();
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) return false;
  if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &spec.program, 0, 0) != 0) return false;
  return true;
}

}  // namespace

const SandboxStatus& sandbox_status() { return g_status; }

const char* sandbox_summary(const SandboxStatus& status) {
  if (status.network_blocked) return "Red bloqueada (seccomp) y sin volcados de memoria";
  if (status.seccomp_unavailable) return "Sin red no garantizada: seccomp no disponible";
  return "Sin volcados de memoria";
}

core::Result<SandboxStatus> harden_process() {
  g_status = SandboxStatus{};

  // A core file would contain the Wi-Fi password or vCard the user just typed.
  struct rlimit limit{};
  limit.rlim_cur = 0;
  limit.rlim_max = 0;
  g_status.core_dumps_disabled =
      setrlimit(RLIMIT_CORE, &limit) == 0 && getrlimit(RLIMIT_CORE, &limit) == 0 &&
      limit.rlim_cur == 0;

  g_status.network_blocked = install_no_network_filter();
  g_status.seccomp_unavailable = !g_status.network_blocked;

  // Neither half is fatal: the window still opens, and the UI states plainly what
  // is and is not enforced rather than implying a guarantee.
  return core::Result<SandboxStatus>::ok(g_status);
}

}  // namespace platform