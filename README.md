# YamOS

YamOS is an experimental x86_64 operating system built around **YamKernel**, a graph-based hybrid kernel. It boots with Limine, brings up a modern kernel core, and layers a small desktop/userland on top of YamGraph, a live resource graph where tasks, memory, devices, files, channels, and capabilities are modeled as connected nodes.

Current tree: **v0.4.0 development line**.

## Not Complete Yet

YamOS cannot yet run arbitrary x86_64 Linux/Windows software or install large
browser engines and language runtimes as-is. These are required foundations:

- persistent root/system volume
- full POSIX/Linux syscall compatibility
- full `execve` and a stronger process model
- dynamic linker / shared libraries
- threads and signals
- real permissions/users/security
- TLS/HTTPS + certificate store
- nonblocking sockets, `select`/`epoll`
- package manager/downloader
- full browser engine support
- language runtime porting layer
- real hardware driver coverage beyond the current QEMU-focused path

Missing compatibility details are tracked in `KERNEL_CAPABILITY_MATRIX.md`.
Important missing surfaces include full process replacement (`execve`), dynamic
linking, pthread/TLS, signals, file permissions, file-backed `mmap`,
nonblocking sockets with `select`/`epoll`, PTYs/termios, TLS/certificates,
package signatures, browser-engine storage/sandboxing, and broader hardware
drivers.

## What Is In This Repo

| Area | Status | Notes |
| --- | --- | --- |
| Boot | Stable | Limine BIOS/UEFI ISO, YamBoot menu, framebuffer splash, module loading. |
| CPU | In-tree | GDT/IDT/TSS, CPUID, MSRs, SYSCALL/SYSRET, NX/SMEP/SMAP/UMIP/WP, APIC/IOAPIC, PIT/RTC, HPET discovery, TSC capability reporting. |
| SMP | Partial | Limine starts AP cores; APs initialize and park. Local APIC IPI primitives exist; scheduler currently uses CPU 0 only. |
| Memory | In-tree | Zone-aware PMM, VMM, heap, slab, CoW/fork support, `brk`, `mmap`, `mprotect`, kernel/user stack guards, TLB shootdown hooks. |
| Scheduler | In-tree | CFS-style scheduler, wait queues, mutexes, futexes, cgroups, OOM, idle/power hooks. |
| Syscalls | In-tree | File, process, memory, scheduler, Wayland, driver, AI, touch, YamGraph IPC, OS info, app registry, TCP sockets, VFS ELF spawn, user-space `waitpid` status copy-out, `stat`/`fstat`, `ftruncate`, `rename`, first `*at` path calls, `unlink`, and `readdir` calls. |
| Filesystems | In-tree | VFS with initrd root, writable ramfs mounts, devfs, procfs, per-process cwd, relative paths, `openat`-style dirfd resolution, open existence checks, `O_APPEND` writes, metadata-backed `SEEK_END`, directory listing, delete, rename, create/truncate/ftruncate, basic file metadata, FAT32 read/write/unlink driver code, block core with QEMU virtio-blk disk registration, MBR/GPT FAT32 discovery, and auto-mounted block FAT32 volumes under `/mnt`. |
| Networking | In-tree | e1000 path plus ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP state-machine code, first fd-backed TCP socket ABI, plain HTTP, certificate-store bootstrap, and bounded TLS ClientHello probe. |
| USB/Input | In-tree | XHCI controller path, USB core, HID, keyboard, mouse, evdev, touch, gestures. |
| PCI/Drivers | In-tree | Bridge-aware PCI scan, command/status helpers, safe BAR sizing, MSI/MSI-X capability discovery, and driver inventory binding. |
| Desktop | In-tree | Wayland-style compositor, polished first-boot setup/login, File Manager, calendar/time/status bar, quick settings, top menu, dock/taskbar, standard window controls, maximize/restore, windows, VTTY mode. |
| Userland | In-tree | Static Ring 3 ELF apps/services for `authd` and `/bin/hello`, libc/libyam syscall support, native app manifests, argv/envp-aware VFS-backed app spawn, and kernel app registry. Main desktop tools are compositor-native kernel services. |
| AI/ML | In-tree | Tensor allocation and accelerator abstraction syscalls. |

## Current Desktop Behavior

- First boot opens a full setup experience for computer name, username, and password.
- Setup now persists to `/var/lib/yamos/system.pro` after account creation or bypass; later boots load the saved device/account profile and go to login instead of asking for computer name, username, and password again.
- Press `Ctrl+Shift+Y` on the setup or login screen to auto-create default users and enter the desktop.
- Bypass defaults:
  - `root / password`
  - `guest / guest`
- Terminal, Browser, and Calculator currently launch as compositor-native apps for reliable drawing.
- Browser now has a real plain-HTTP path through kernel DNS/TCP/HTTP, a modern toolbar/address bar, back/forward/reload controls, history, response status display, a scrollable static document paint layer, and first inline color/background style handling for headings, paragraphs, list items, links, pre/code blocks, buttons, form placeholders, and image placeholders. Full encrypted HTTPS page loading, JavaScript, full CSS layout, real image decoding, and Firefox-class multi-process rendering remain future work.
- Calculator is a compositor-native desktop utility with standard/scientific modes, fixed-decimal arithmetic, memory register, history, keyboard input, and compositor clipboard copy/paste.
- File Manager launches from the dock launcher or File menu and now has an Explorer-style shell with sidebar locations, back/forward/up navigation, editable address and search fields, sortable details view, item details pane, create file/folder actions, file delete, and an integrated text editor for VFS files.
- The desktop bar shows BDT calendar/time, wired network status from the kernel network interface, Wi-Fi driver state, and audio mixer state; clicking time opens the calendar and clicking status chips opens quick settings.
- Terminal file commands now use the same VFS: `ls`, `cat`, `mkdir`, `touch`, `rm`, and `write /home/root/file.txt text`.
- Terminal can launch the sample static ELF app with `run /bin/hello arg...` or direct `hello arg...`.
- Terminal supports `cd` and `pwd`; VFS paths now resolve relative to each task's current working directory.
- App windows use right-side minimize, maximize/restore, and close controls; minimized apps restore from the dock.
- The visible Terminal is `src/os/services/compositor/wl_terminal.c`.
- Language-specific runtime installers are not part of the current tree; the
  project is focused on kernel, drivers, memory, storage, networking, and OS
  services.
- Terminal commands `installer status`, `install`, and `install kernel-net` inspect or request the generic OS capability probe.
- Shifted characters such as `Shift+9 = (`, `Shift+0 = )`, and uppercase letters are routed through evdev to focused apps.

## Quick Start

On Ubuntu/WSL:

```bash
make setup
make iso
make run
```

Useful targets:

```bash
make iso              # Build build/yamkernel.iso
make run              # Run QEMU BIOS mode
make run-uefi         # Run QEMU UEFI mode
make run-serial       # Write serial output to build/serial.log
make run-serial-only  # Headless serial console
make debug            # QEMU paused with GDB server on localhost:1234
make clean
```

Build outputs:

- `build/yamkernel.elf`
- `build/yamkernel.iso`
- `build/authd.elf`
- `build/hello.elf`
- raw splash assets under `build/logo.bin` and `build/wallpaper.bin`

## Architecture

```text
+---------------------------- Ring 3 --------------------------------+
| authd service | /bin/hello VFS ELF | libc + libyam wrappers      |
+----------------------- SYSCALL / SYSRET ---------------------------+
| File, process, memory, scheduler, Wayland, driver, AI, IPC, app ABI |
+---------------------------- Ring 0 --------------------------------+
| Scheduler | VFS | Net | USB | DRM/Compositor | cgroups | OOM | AI   |
+-------------------------- YamGraph --------------------------------+
| Nodes: tasks, memory, devices, files, channels, namespaces          |
| Edges: owns, maps, depends, channel, capability                     |
+----------------------- Memory / CPU / Boot ------------------------+
| PMM | VMM | Heap | Slab | GDT/IDT | APIC | ACPI | Limine | YamBoot |
+--------------------------------------------------------------------+
```

## Source Tree Highlights

```text
src/boot/                 YamBoot menu
src/kernel/               kernel entry, shell, panic handling
src/kernel/api/           syscall numbers and public ABI structs
src/cpu/                  GDT/IDT/APIC/ACPI/SMP/syscall/security
src/mem/                  PMM, VMM, heap, slab, OOM
src/sched/                scheduler, wait queues, cgroups, user entry
src/fs/                   VFS, FAT32, initrd, ELF loader, poll
src/ipc/                  IPC scaffolding
src/net/                  ARP/IP/ICMP/UDP/TCP/DHCP/DNS stack
src/drivers/              PCI, USB, input, serial, timer, video, DRM, net
src/nexus/                YamGraph, channels, capabilities
src/os/apps/              authd, hello sample app, and user linker script
src/os/lib/               libc and libyam
src/os/README.md          OS-layer layout and app-facing structure rules
src/os/dev/               devfs and virtual TTYs
src/os/proc/              procfs
src/os/services/          compositor and OS services
```

## Application Architecture

- `src/kernel/api/syscall.h` is the public ABI source of truth for kernel and userspace.
- `src/os/lib/libyam/app.h` exposes the native app SDK: `yam_os_info()`, `yam_app_register()`, `yam_app_query()`, and `yam_spawn()`.
- `src/os/services/app_registry/` records Ring 3 app/service manifests in the kernel with PID, YamGraph node, app type, requested permissions, name, publisher, version, and description.
- `authd` now registers itself as a native YamOS service before using YamGraph IPC.
- `/bin/hello` is packaged as `hello.elf`, registered into initrd, and launched through the argv/envp-aware VFS-backed ELF spawn path. Bare executable names resolve through `/bin`, `/usr/local/bin`, `/opt/yamos/packages`, and `/home/root/bin`.
- See `APP_ARCHITECTURE.md` for the app model and the next steps toward installable developer apps.
- Userland now has first TCP socket ABI numbers and libc wrappers for `socket`, `bind`, `connect`, `listen`, `accept`, `send`, `recv`, `sendto`, and `recvfrom`; currently AF_INET/SOCK_STREAM TCP is implemented, while datagram UDP remains future work.

## Boot Flow

1. Limine loads the kernel, framebuffer, and ELF/assets modules.
2. YamBoot shows Normal, Safe Mode, and Reboot choices.
3. The kernel initializes framebuffer, CPU tables, security flags, memory, ACPI/APIC/SMP, HPET/TSC detection, and syscalls.
4. YamGraph is initialized and core subsystems are registered.
5. Drivers and subsystems start unless Safe Mode was selected.
6. Scheduler, cgroups, OOM, power, AI, PID 1, and the compositor are spawned.
7. The BSP idle loop yields; AP cores stay initialized and parked.

## Runtime Notes

- Safe Mode skips several driver/subsystem init paths for easier boot triage.
- `YAM_PREEMPTIVE` and `YAM_WAYLAND` are enabled in `src/kernel/main.c`.
- `YAM_DEMO_TASKS` is disabled by default.
- The desktop compositor starts with first-boot setup/login, can spawn apps, and includes a VTTY render mode.
- `Ctrl+Shift+Y` bypasses setup/login by creating default accounts and logging into the desktop.
- Terminal installer commands now route to the generic OS installer/capability service. TLS/certificate capability is probed with an outbound TLS ClientHello and ServerHello check; persistent package installation still needs package signatures and downloader/install transactions.
- Browser engines, language runtimes, and package tools should use the fd-backed socket ABI instead of private kernel-only network helpers as that ABI matures.
- First missing-compatibility slice added: per-process current working directory, `SYS_CHDIR`, `SYS_GETCWD`, libc `chdir/getcwd`, and relative VFS path resolution.
- File Manager writes through VFS. With the QEMU virtio FAT32 disk attached, `/home`, `/var`, and `/usr/local` are persistent; without it they fall back to ramfs.
- VFS `open()` now returns failure for missing paths unless `O_CREAT` is supplied, while `/dev` and `/proc` only open known pseudo-files.
- libc `stat()`/`fstat()` now route to kernel VFS metadata instead of guessing file type in user space.
- libc `ftruncate()` now routes to `SYS_FTRUNCATE` for RAMFS/FAT32 file resizing.
- libc `rename()` now routes to `SYS_RENAME`; RAMFS renames nodes in place, and FAT32 regular-file rename currently uses copy-then-unlink.
- First libc `*at` wrappers now route to kernel dirfd-aware syscalls: `openat`, `fstatat`, `mkdirat`, `unlinkat`, and `renameat`.
- VFS honors `O_APPEND` before each regular file write, so libc append modes such as `fopen(path, "a")` append instead of overwriting from offset zero.
- `lseek(fd, 0, SEEK_END)` now uses VFS metadata through `fstat`, so EOF seeking works for initrd, RAMFS, FAT32, and other stat-backed files.
- QEMU runs attach `build/yamos-fat32.disk` as `vd0`; YamOS auto-mounts FAT32-compatible virtio disks at `/mnt/vd0`, exposes mounted volumes in `/mnt`, and promotes `/home`, `/var`, and `/usr/local` to the FAT32 disk when available.
- The `/bin/hello` boot probe now validates the public app process path by spawning another `hello` from Ring 3 and reaping it through libc `waitpid()`.
- AP cores are initialized and can receive kernel IPIs, but full multi-core task scheduling remains disabled until address-space switching and run-queue ownership are audited.
- TSC-deadline is detected when the CPU exposes it. The current timer path still uses the calibrated periodic APIC timer unless a later platform-specific timer switch is added.
- CPU exceptions print register state, and the panic path has a register-frame variant for fatal exception debugging.

## Documentation

- `developer.html` - current architecture and subsystem reference.
- `APP_ARCHITECTURE.md` - native app/service ABI and developer model.
- `BROWSER_FIREFOX_GAP.md` - blunt comparison of YamBrowser against Firefox-class browser requirements.
- `DEBUGGING.md` - build, QEMU, serial, GDB, and triage guide.
- `future_plan.txt` - updated roadmap from the v0.4 tree toward v1.0.
- `implementation_plan.md` and `task.md` - implementation planning/history.

## License

MIT
