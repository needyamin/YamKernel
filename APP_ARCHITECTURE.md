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

## Next Work

To support powerful third-party apps, the kernel and OS still need:

- persistent root/system volume
- broader socket syscalls over the existing TCP/UDP stack
- executable loader path from filesystem, not only boot modules
- process credentials and per-user permissions
- dynamic linker or a stable static SDK profile
- app package format with manifest, files, signatures, and install database
- capability checks for app-declared permissions
- user-mode driver isolation and service restart policy
- TLS/HTTPS and certificate store for safe downloads
