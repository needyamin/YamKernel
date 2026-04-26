# YamKernel — User Manual

A short, practical guide to building, booting, and using YamKernel.

---

## 1. Quick Start

```bash
make setup         # one-time: nasm, gcc, xorriso, qemu, limine
make iso           # build bootable yamkernel.iso
make run           # boot in QEMU (serial → stdio)
make run-uefi      # boot in QEMU with UEFI (OVMF)
make clean         # wipe build/
```

Outputs land in `build/`:
- `yamkernel.elf` — kernel binary
- `yamkernel.iso` — bootable ISO (QEMU / VirtualBox / VMware / USB)

Boot on real hardware:
```bash
sudo dd if=build/yamkernel.iso of=/dev/sdX bs=4M status=progress
```

---

## 2. Boot Flow

1. **Limine** loads the kernel in 64-bit long mode.
2. **YamBoot** menu (polled keyboard):
   - `1` Normal — full driver stack
   - `2` Safe Mode — only PIT + keyboard + shell
   - `3` Reboot — 8042 reset
3. CPU init: GDT, IDT, CPUID, **NX / SMEP / SMAP / UMIP / WP**.
4. Memory: VMM → PMM (Cell Allocator) → Heap → **Slab**.
5. **ACPI** (MADT) → **LAPIC + IO-APIC** → per-CPU data → SMP enumerate → `syscall_init`.
6. YamGraph + self-tests.
7. Drivers (PCI, USB, I2C, SPI, VFS, IPC, NET, mouse) — Normal mode only.
8. **Scheduler**: spawn demo threads + Ring-3 demo, start APIC timer @ 100 Hz, enable preemption.
9. `shell_start()` runs as task #0; CFS-lite preempts via APIC timer.

---

## 3. Expected Boot Output (key lines)

```
[YAM] Serial console initialized
[FB] Framebuffer: 1280x800 @ 32 bpp
[SEC] NX     enabled
[SEC] SMEP   enabled
[SEC] SMAP   enabled
[ACPI] CPUs=N  LAPIC=0x...
[APIC] LAPIC enabled, timer calibrated, ticks/ms=...
[SLAB] cache 'task_t' obj=... align=16
[SCHED] init OK (CFS-lite, weights=8/4/2/1)
[SYSCALL] entry=... STAR=0x...
[USER] mapped 22 bytes @ 0x400000, stack @ 0x800000
[hb-A] tick (vruntime-driven sleep)
[hb-B] tick (vruntime-driven sleep)
[USER] hi from ring3
yam@kernel ~ %
```

---

## 4. Shell Commands

At the `yam@kernel ~ %` prompt:

| Command | Description |
|---------|-------------|
| `help` | Full command reference |
| `top` | Live dashboard (CPU / MEM / NET / SYS) |
| `mem` | Cell Allocator state + usage bar |
| `cpu` | CPU vendor, brand, feature flags |
| `pci` / `lspci` | Enumerated PCI devices |
| `graph` | YamGraph nodes + edges + permissions |
| `net` / `ifconfig` | Network interfaces & protocols |
| `ipc` | IPC mechanism status |
| `fs` | Mounted volumes / supported filesystems |
| `uptime` | System running time |
| `date` | RTC date/time |
| `uname` / `ver` / `version` | Kernel version |
| `whoami` | Current shell user |
| `echo <text>` | Print text |
| `clear` | Clear framebuffer |
| `reboot` / `restart` | PS/2 + ACPI + triple-fault fallback |
| `shutdown` | ACPI shutdown |

Navigation: ↑ / ↓ cycles up to 15 history entries.

---

## 5. Subsystems Cheat Sheet

### Memory
- `kmalloc(size)` / `kfree(ptr)` — general heap.
- `kmem_cache_create(name, size, align)` → `kmem_cache_alloc/free` — fast fixed-size pools.
- `pmm_alloc_page()` / `pmm_free_page(phys)` — raw 4 KiB physical pages.
- `vmm_map(virt, phys, flags)` — 4-level paging, huge-page aware.

### Scheduling (CFS-lite)
- `sched_spawn(name, fn, arg, prio)` — kernel thread, prio `0..3` (`0`=highest, weights `8/4/2/1`).
- `sched_yield()` — voluntary yield.
- `sched_current()` — current `task_t *`.
- Preemption: APIC timer @ 100 Hz, `vruntime += 1024 / weight` per tick.

### Sync
- `spinlock_t` — `spin_lock` / `spin_unlock` / `spin_lock_irqsave` / `spin_unlock_irqrestore`.
- `mutex_t` — blocking, `mutex_lock` / `mutex_unlock`.
- `wait_queue_t` — `wq_sleep` / `wq_wake_one` / `wq_wake_all`.
- `task_sleep_ms(ms)` — block until APIC ticks elapse.

### User Mode (Ring 3)
- Syscalls: `SYS_WRITE=1`, `SYS_EXIT=2`, `SYS_GETPID=3`, `SYS_YIELD=4`.
- ABI: `rax`=nr, args in `rdi, rsi, rdx, r10, r8, r9` (SYSCALL convention).
- Demo program: `src/sched/user_demo.asm` loops `SYS_WRITE` + `SYS_YIELD`.

---

## 6. Adding Code

| Want to add… | Do this |
|---|---|
| A kernel thread | `sched_spawn("name", fn, arg, prio)` from anywhere after `sched_enable()`. |
| A timed wait | `task_sleep_ms(ms);` instead of busy-loop. |
| A protected section | `spinlock_t lk = SPINLOCK_INIT; spin_lock(&lk); … ; spin_unlock(&lk);`. |
| A blocking critical region | `mutex_t m = MUTEX_INIT; mutex_lock(&m); … ; mutex_unlock(&m);`. |
| A new syscall | Add `SYS_*` in `cpu/syscall.h`, handle in `syscall_dispatch` (`cpu/syscall.c`). |
| A new shell command | Edit `kernel/shell.c` — register name + handler. |
| A new driver | Drop a `.c/.h` under `src/drivers/<bus>/`; Makefile auto-discovers. |

---

## 7. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| Boot hangs after Limine logo | Likely PMM/VMM/ACPI — check serial log (`make run` shows it on stdio). |
| `[YAM] FATAL: Limine base revision mismatch` | Rebuild with current `vendor/limine.h`. |
| No `[USER] hi from ring3` | Ensure `syscall_init()` and `user_demo_load()` ran; check `[SYSCALL]` line. |
| Demo threads don't run | Confirm `sched_enable()` and `apic_timer_start(100)` are reached; APIC timer must calibrate. |
| Triple fault on user entry | Check `gdt.c` user CS/SS layout (must match SYSRET: CS=0x23, SS=0x1B). |
| SMAP fault in `sys_write` | Kernel must `STAC` before reading user buffer, `CLAC` after. |
| `make` fails: `x86_64-elf-gcc not found` | Native `gcc` is auto-fallback on Linux/WSL; on bare Windows use WSL or install the cross toolchain. |

---

## 8. Project Layout (short)

```
src/
  boot/      YamBoot menu
  kernel/    main.c, panic, shell
  cpu/       gdt, idt, cpuid, security, acpi, apic, percpu, smp, syscall
  sched/     sched (CFS-lite), wait, context.asm, enter_user.asm, user.c, demos
  mem/       pmm (Cell), vmm, heap, slab
  nexus/     YamGraph, capabilities, channels
  drivers/   bus (PCI/USB/I2C/SPI), input, net, serial, timer, video, bluetooth
  net/       TCP/UDP/ICMP/ARP/DHCP/DNS scaffolding
  ipc/  fs/  lib/  include/nexus/
```

See `README.md` for the architecture deep-dive.
