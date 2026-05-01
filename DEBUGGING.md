# YamOS Debugging and Testing Guide

This guide covers the current v0.4 development tree: Limine boot, YamBoot, SMP AP parking, APIC IPIs, TLB shootdown hooks, HPET/TSC detection, guarded stacks, VFS/initrd/devfs/procfs, networking, USB/input, VTTY, and the Wayland-style compositor.

## Fast Build and Run

Windows PowerShell:

```powershell
.\scripts\test.ps1
.\scripts\test.ps1 serial
.\scripts\test.ps1 headless
.\scripts\test.ps1 debug
```

WSL/Linux:

```bash
./scripts/test.sh
./scripts/test.sh serial
./scripts/test.sh headless
./scripts/test.sh debug
```

Direct Makefile targets:

```bash
make iso
make run
make run-uefi
make run-serial
make run-serial-only
make debug
```

## Serial First

Most useful diagnostics go to COM1. Prefer serial logs when the framebuffer, compositor, or input stack is suspect.

```bash
make run-serial
tail -f build/serial.log
```

Headless serial:

```bash
make run-serial-only
```

In QEMU monitor style consoles, use `Ctrl+A` then `X` to exit.

## GDB

Start QEMU frozen:

```bash
make debug
```

Attach:

```bash
gdb build/yamkernel.elf
(gdb) target remote localhost:1234
(gdb) break kernel_main
(gdb) continue
```

Useful early breakpoints:

- `kernel_main`
- `smp_init`
- `apic_handle_tlb_shootdown`
- `vmm_alloc_kernel_stack`
- `syscall_dispatch`
- `vfs_init`
- `wl_compositor_task`
- `kpanic`
- `kpanic_with_frame`

## Kernel Logging

Use `src/lib/kdebug.h`:

| Macro | Level | Use |
| --- | --- | --- |
| `KTRACE(tag, fmt, ...)` | 0 | Very noisy flow traces. |
| `KDBG(tag, fmt, ...)` | 1 | Internal state and topology. |
| `KINFO(tag, fmt, ...)` | 2 | Boot progress and subsystem init. |
| `KWARN(tag, fmt, ...)` | 3 | Recoverable problems. |
| `KERR(tag, fmt, ...)` | 4 | Fatal errors and panic path. |

Common tags:

`BOOT`, `INIT`, `SMP`, `APIC`, `HPET`, `TSC`, `VMM`, `VFS`, `FAT32`, `XHCI`, `USB`, `HID`, `NET`, `TCP`, `WAYLAND`, `WL_DBG`, `VTTY`, `SCHED`, `CGROUP`, `OOM`, `POWER`, `AI`, `TOUCH`, `GESTURE`, `YAMGRAPH`.

Example:

```c
#include "lib/kdebug.h"

KINFO("VFS", "mounted %s at %s", fs_type, mount_point);
KERR("TCP", "socket %d reset", fd);
```

## Boot Triage

YamBoot gives three paths:

- Normal: full boot with drivers, scheduler, PID 1, and compositor.
- Safe Mode: skips several driver/subsystem init paths after the core kernel is online.
- Reboot: asks the keyboard controller to reset the machine.

For early boot failures, compare Normal and Safe Mode. If Safe Mode boots, focus on Phase 9 driver/subsystem initialization in `src/kernel/main.c`.

## Current Boot Phases

The boot log should roughly progress through:

1. Serial and Limine validation.
2. Framebuffer and YamBoot.
3. Splash/module scan.
4. CPU setup: GDT, IDT, CPUID, TSC reporting, security.
5. Memory: VMM, PMM, heap.
6. ACPI, LAPIC, IOAPIC, HPET discovery/init, per-CPU, SMP, syscall.
7. YamGraph and self-tests.
8. Drivers/subsystems: PCI, USB, VFS, IPC, NET, input.
9. Scheduler, cgroups, OOM, power, AI.
10. PID 1 and Wayland compositor.

## Panic Checklist

| Symptom | First checks |
| --- | --- |
| `#GP` | Segment selectors, SYSRET GDT layout, non-canonical pointers. |
| `#PF` | CR2, page flags, HHDM/MMIO mapping, user/kernel bit. |
| `#UD` | Jumped through corrupt function pointer or bad stack return. |
| Double fault | TSS `rsp0`, kernel stack, recursive exception path. |
| Hang after SMP | AP boot wait, AP count, `g_aps_booted`, parked AP loop. |
| TLB/IPI issue | `APIC_VEC_TLB`, `apic_handle_tlb_shootdown`, local APIC ICR delivery status. |
| Stack overflow | Guard-page fault near guarded kernel stack or user stack boundaries. |
| Hang after VFS | initrd mount, task fd table, `/dev` or `/proc` read path. |
| Blank desktop | DRM buffer allocation, wallpaper module, compositor state, page flip. |

## Subsystem Tips

SMP:

- APs are booted by Limine and parked after per-core setup.
- Local APIC IPI primitives are present, including a TLB shootdown vector at `0xF0`.
- TLB shootdown currently broadcasts invalidations to other active APIC CPUs; AP task scheduling is still disabled.
- `smp_sched_cpu_count()` currently reports the schedulable domain.
- Scheduler bugs should be debugged as single-run-queue bugs first.

Timers:

- PIT remains initialized for legacy timing and calibration support.
- HPET is parsed from ACPI and initialized when the `HPET` table exists.
- TSC and TSC-deadline capability are logged during CPU setup. In the default QEMU profile, TSC-deadline may report unavailable.
- The active scheduler tick is still the calibrated periodic Local APIC timer.

Memory:

- Kernel task stacks allocated through the scheduler use unmapped guard pages on both sides.
- User ELF stacks reserve adjacent unmapped guard pages.
- Fatal CPU exceptions call the panic-with-frame path, so serial output should include a register dump.

VFS/FAT32:

- `vfs_init()` mounts `/`, `/dev`, and `/proc`.
- FAT32 code exists, but a real block device or disk image must be mounted before FAT paths can be exercised.
- Stdout/stderr fallback through `sys_write` goes to framebuffer text output.

Networking:

- Use QEMU with e1000 networking when testing DHCP/TCP paths.
- Watch for `[NET]`, `[DHCP]`, `[ARP]`, `[IP]`, `[TCP]`, and e1000 logs.

USB/Input:

- For USB tests, boot QEMU with XHCI/HID devices.
- PS/2 keyboard and mouse paths are separate from USB HID.

Compositor/VTTY:

- The compositor starts in login state.
- VTTY has six buffers and renders through framebuffer helpers.
- Input routing is split between login, desktop, dock/menu, focused windows, and VTTY.

AI/Touch:

- Use AI syscalls to exercise tensor allocation and job submission.
- Touch/gesture paths are easiest to debug with verbose `TOUCH` and `GESTURE` logs.

## Useful QEMU Commands

Network:

```bash
qemu-system-x86_64 -cdrom build/yamkernel.iso -m 256M -smp 2 \
  -netdev user,id=n0 -device e1000,netdev=n0 -serial stdio
```

USB HID:

```bash
qemu-system-x86_64 -cdrom build/yamkernel.iso -m 256M \
  -device nec-usb-xhci -device usb-kbd -device usb-mouse -serial stdio
```

Headless smoke test:

```bash
timeout 25 qemu-system-x86_64 -cdrom build/yamkernel.iso -m 256M \
  -serial stdio -display none -no-reboot
```

## Known Development Caveats

- AP cores are initialized but parked; full multi-core scheduling is future work.
- IPI and TLB shootdown primitives exist, but they are not yet proof of full SMP scheduling correctness.
- HPET and TSC-deadline support are discovery/capability pieces; timer policy still uses the periodic APIC timer today.
- FAT32 needs a mounted backing device/image to be useful outside unit-style memory tests.
- TCP is an in-tree implementation, not yet a hardened production stack.
- Some userland headers and syscall wrappers are ahead of the kernel dispatcher; keep ABI changes synchronized.
- The compositor is CPU-rendered and VM performance depends heavily on resolution.
