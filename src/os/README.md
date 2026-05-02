# YamOS OS Layer

`src/os` is the application-facing personality layer above YamKernel. Keep this
tree boring and stable: future software should depend on the public ABI, libc,
libyam, VFS paths, and service contracts here instead of private kernel
implementation details.

## Layout

```text
src/os/apps/       Ring 3 programs loaded as ELF modules or future packages
src/os/bin/        PID 1 and base command/process entry points
src/os/dev/        devfs devices and virtual terminal endpoints
src/os/proc/       procfs runtime information endpoints
src/os/lib/libc/   POSIX-style C compatibility headers and wrappers
src/os/lib/libyam/ Native YamOS SDK wrappers and app/service manifest API
src/os/services/   Long-running OS services such as compositor and installer
```

## Developer Contract

- `src/kernel/api/syscall.h` is the syscall number and ABI struct source of
  truth.
- `src/os/lib/libc` is for portable C/POSIX-style code.
- `src/os/lib/libyam` is for native YamOS features such as app registration,
  OS feature discovery, YamGraph IPC, and future package/service APIs.
- Applications should store user data under `/home/<user>`, service/cache data
  under `/var`, optional app payloads under `/opt`, and temporary files under
  `/tmp`.
- Hardware and kernel resources should be reached through syscalls, `/dev`,
  `/proc`, libyam, or declared YamGraph capabilities. Do not expose private
  driver structs as an app ABI.

## Professional Structure Rules

- Public ABI changes must be additive unless `YAM_ABI_VERSION` is bumped.
- New OS services need a small public header, a stable status/query API, and
  boot log lines that report ready/blocked state clearly.
- User-visible tools must report real blockers. Avoid placeholder success
  paths that hide missing kernel features.
- Kernel-private helpers stay outside `src/os/lib`; app-facing wrappers stay
  inside `src/os/lib`.
- Prefer standard path behavior where possible: cwd-aware paths, `/dev` for
  devices, `/proc` for runtime facts, `/home` for users, `/var` for state, and
  `/mnt` for discovered volumes.

## Current Gaps

The shape is suitable for a serious OS project, but the compatibility contract
is not complete yet. The next big developer-platform gaps are `execve`, dynamic
linking, threads, signals, UID/GID and permissions, nonblocking sockets,
TLS/certificates, package transactions, and broader hardware drivers.
