# YamOS

YamOS is an experimental x86_64 operating system built around **YamKernel**, a graph-based hybrid kernel. It boots with Limine, brings up a modern kernel core, and layers a small desktop/userland on top of YamGraph, a live resource graph where tasks, memory, devices, files, channels, and capabilities are modeled as connected nodes.

Current tree: **v0.4.0 development line**.

## What Is In This Repo

| Area | Status | Notes |
| --- | --- | --- |
| Boot | Stable | Limine BIOS/UEFI ISO, YamBoot menu, framebuffer splash, module loading. |
| CPU | In-tree | GDT/IDT/TSS, CPUID, MSRs, SYSCALL/SYSRET, NX/SMEP/SMAP/UMIP/WP, APIC/IOAPIC, PIT/RTC, HPET discovery, TSC capability reporting. |
| SMP | Partial | Limine starts AP cores; APs initialize and park. Local APIC IPI primitives exist; scheduler currently uses CPU 0 only. |
| Memory | In-tree | Zone-aware PMM, VMM, heap, slab, CoW/fork support, `brk`, `mmap`, `mprotect`, kernel/user stack guards, TLB shootdown hooks. |
| Scheduler | In-tree | CFS-style scheduler, wait queues, mutexes, futexes, cgroups, OOM, idle/power hooks. |
| Syscalls | In-tree | File, process, memory, scheduler, Wayland, driver, AI, touch, and YamGraph IPC calls. |
| Filesystems | In-tree | VFS with initrd root, devfs, procfs, and FAT32 read/write driver code. |
| Networking | In-tree | e1000 path plus ARP, IPv4, ICMP, UDP, DHCP, DNS, and TCP state-machine code. |
| USB/Input | In-tree | XHCI controller path, USB core, HID, keyboard, mouse, evdev, touch, gestures. |
| Desktop | In-tree | Wayland-style compositor, login screen, top menu, dock/taskbar, windows, VTTY mode. |
| Userland | In-tree | ELF modules for terminal, calculator, browser, Python status app, authd, and drivers. |
| AI/ML | In-tree | Tensor allocation and accelerator abstraction syscalls. |

## Current Desktop Behavior

- First boot opens a setup overlay for computer name, username, and password.
- Press `Ctrl+Shift+Y` on the setup or login screen to auto-create default users and enter the desktop.
- Bypass defaults:
  - `root / password`
  - `guest / guest`
- Terminal, Browser, and Calculator currently launch as compositor-native apps for reliable drawing.
- The visible Terminal is `src/os/services/compositor/wl_terminal.c`.
- Real Python work now targets python.org CPython: official CPython 3.14.4 source is vendored at `vendor/cpython/Python-3.14.4`, with YamOS port notes in `src/os/ports/python/cpython`.
- In the main Terminal, `python`, `python3`, `py`, `pip`, and `pip3` are reserved for python.org CPython only. Until CPython is linked, they show CPython port status instead of running a fake interpreter.
- The separate `python.elf` module is currently a CPython port-status app; normal workflow remains one Terminal only.
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
- user modules under `build/*.elf`
- raw splash assets under `build/logo.bin` and `build/wallpaper.bin`

## Architecture

```text
+---------------------------- Ring 3 --------------------------------+
| ELF apps: terminal, calculator, browser, Python, authd, drivers     |
| libc + libgui + libyam syscall wrappers                             |
+----------------------- SYSCALL / SYSRET ---------------------------+
| File, process, memory, scheduler, Wayland, driver, AI, IPC syscalls |
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
src/os/apps/              user-mode apps compiled as ELF modules
src/os/lib/               libc, libgui, libyam
src/os/dev/               devfs and virtual TTYs
src/os/proc/              procfs
src/os/services/          compositor service and demo clients
```

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
- Terminal `python` and `pip` are integrated into the compositor-native Terminal as CPython-only commands. Real python.org CPython 3.14.4 source is present and the YamOS port notes are under `src/os/ports/python/cpython`.
- AP cores are initialized and can receive kernel IPIs, but full multi-core task scheduling remains disabled until address-space switching and run-queue ownership are audited.
- TSC-deadline is detected when the CPU exposes it. The current timer path still uses the calibrated periodic APIC timer unless a later platform-specific timer switch is added.
- CPU exceptions print register state, and the panic path has a register-frame variant for fatal exception debugging.

## Documentation

- `developer.html` - current architecture and subsystem reference.
- `DEBUGGING.md` - build, QEMU, serial, GDB, and triage guide.
- `future_plan.txt` - updated roadmap from the v0.4 tree toward v1.0.
- `implementation_plan.md` and `task.md` - implementation planning/history.

## License

MIT
