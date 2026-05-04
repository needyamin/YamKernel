# YamOS Application Architecture

This file defines the first stable slice of the YamOS application layer. The
goal is a hybrid model that feels familiar to Linux, Windows, and macOS
developers without copying any one system blindly.

## Layers

```text
--------------------------- Applications ----------------------------+
| GUI apps | CLI tools | background services | runtimes | app drivers |
+----------------------------- SDK ----------------------------------+
| libc for POSIX-style code | libyam for native YamOS services/API     |
+----------------------- SYSCALL / SYSRET ---------------------------+
| process | memory | VFS | scheduler | GUI | IPC | installer | device |
+-------------------------- Kernel Core -----------------------------+
| YamGraph registry | scheduler | VFS | net | drivers | memory | CPU   |
+--------------------------------------------------------------------+
```

## Developer Contract

- `src/kernel/api/syscall.h` is the public kernel ABI source of truth.
- `src/os/lib/libyam/app.h` is the native app SDK header.
- `src/os/lib/libc/` is the POSIX-style compatibility layer for portable C
  code. It should wrap public syscalls and avoid private kernel internals.
- Apps call `yam_os_info()` to detect ABI version, syscall range, page size,
  CPU count, and enabled kernel features.
- Apps call `yam_app_register()` with a `yam_app_manifest_t` so the kernel can
  track services, GUI apps, runtimes, and driver-style processes.
- Apps can call `yam_spawn(path)`, `yam_spawn_args(path, argv)`, or
  `yam_spawn_env(path, argv, envp)` to launch another static ELF from a VFS
  path. This is an early spawn ABI, not full `execve`: interpreter scripts,
  process replacement, and dynamic linking are still future work.
- Spawn calls resolve bare executable names through `/bin`, `/usr/local/bin`,
  `/opt/yamos/packages`, and `/home/root/bin`.
- Apps can check `YAM_OS_FLAG_VFS_SPAWN` in `yam_os_info().flags` before using
  this launch path.
- Spawned apps can use libc `waitpid()`/`wait()` to reap child processes. The
  scheduler owns zombie reaping and the syscall layer copies encoded wait
  status back to user memory.
- Apps can use libc `stat()`, `lstat()`, and `fstat()` for basic VFS-backed
  metadata such as file type and size.
- Apps can use libc `rename()` for RAMFS files and FAT32 regular files, which
  supports common temp-file-to-final-file workflows.
- Apps can use first-slice `*at` path calls (`openat`, `fstatat`, `mkdirat`,
  `unlinkat`, `renameat`) when working relative to an opened directory fd.
- Apps can rely on `O_APPEND` for regular VFS files, including libc append
  modes such as `fopen(path, "a")`.
- Apps can use `lseek(..., SEEK_END)` on VFS files backed by kernel metadata.
- The kernel records registrations in `src/os/services/app_registry/`.
- Records include PID, YamGraph node id, app type, requested permissions, name,
  publisher, version, and description.
- Stable writable app data belongs under `/home/<user>`, `/var`, or `/opt`.
  Removable and discovered block volumes are exposed under `/mnt/<device>`.
- OS services must expose public ABI through syscalls, libyam, VFS nodes, or
  YamGraph IPC. Applications should not link against compositor internals,
  driver internals, or kernel-private helpers.

## App Types

- `YAM_APP_TYPE_PROCESS`: normal command-line or background process.
- `YAM_APP_TYPE_SERVICE`: long-running OS service.
- `YAM_APP_TYPE_GUI`: windowed compositor client.
- `YAM_APP_TYPE_DRIVER`: user-mode driver or device helper.
- `YAM_APP_TYPE_RUNTIME`: language/runtime host.

## Permissions

Apps declare requested permissions in their manifest:

- `YAM_APP_PERM_FS`
- `YAM_APP_PERM_NET`
- `YAM_APP_PERM_IPC`
- `YAM_APP_PERM_GUI`
- `YAM_APP_PERM_DEVICE`
- `YAM_APP_PERM_DRIVER`
- `YAM_APP_PERM_INSTALL`
- `YAM_APP_PERM_RUNTIME`

This first implementation records requested permissions. The next security
step is capability enforcement: syscalls should check YamGraph edges before
allowing file, network, device, MMIO, driver, or installer operations.

## Current In-Tree Example

`src/os/apps/authd.c` registers itself as:

- type: `YAM_APP_TYPE_SERVICE`
- permissions: `YAM_APP_PERM_IPC`
- name: `authd`

During boot the serial log should include an app registry line when authd
starts and registers through the syscall layer.

`src/os/apps/hello.c` is the first VFS-launched sample app. The build packages
it as `hello.elf`, PID 1 registers the boot module at `/bin/hello`, and then
launches it by bare name through `elf_spawn_resolved_argv_envp("hello", ...)`.
The terminal command `run /bin/hello arg...` or direct `hello arg...` uses the
same VFS-backed ELF path and provides a small shell environment.
The boot probe also exercises user-space `spawnve("hello", ...)` plus
`waitpid()` so the public process ABI is verified, not only PID 1's kernel
launch path. It also probes `stat()` and `fstat()` against initrd and
`/usr/local/bin` executables.

## Next Work

To support powerful third-party apps, the kernel and OS still need:

- persistent root/system volume
- broader socket syscalls over the existing TCP/UDP stack
- full `execve` with interpreter scripts and process replacement
- process credentials and per-user permissions
- dynamic linker or a stable static SDK profile
- app package format with manifest, files, signatures, and install database
- capability checks for app-declared permissions
- user-mode driver isolation and service restart policy
- TLS/HTTPS and certificate store for safe downloads
