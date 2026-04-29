# YamOS

**A Graph-Based Adaptive Operating System** — x86_64, Limine boot, modern Linux-inspired kernel

YamOS is a self-contained OS powered by the **YamKernel v0.3.0**. Every resource — processes, memory, devices, files — lives as a node in a live directed graph (**YamGraph**), with permissions flowing through edges as unforgeable capability tokens.

## Features

| Subsystem | Approach | Status |
|-----------|----------|--------|
| Boot | **YamBoot** — pre-kernel menu (Normal / Safe / Reboot) | Stable |
| Resource Model | **YamGraph** — live directed graph of all resources | Stable |
| Permissions | **Capability tokens** on graph edges | Stable |
| Memory | **Zone-Aware Cell Allocator** (DMA/DMA32/Normal zones), page descriptors, refcount, watermarks | Stable |
| Virtual Memory | 4-level paging, **CoW** (Copy-on-Write), **2MB huge pages**, `mprotect`, `brk` | Stable |
| Heap | **Size-class bucket allocator** (O(1) small alloc) + auto-expansion | Stable |
| Slab | **Per-CPU magazine** layer, partial/full/empty lists, shrink under pressure | Stable |
| Scheduler | **Multi-Queue CFS** — O(log n) sorted pick, per-CPU queues, Linux nice-to-weight table | Stable |
| Process Mgmt | `fork()` (CoW), `waitpid()`, `kill()`, signals, zombie reaping | Stable |
| Sync | Spinlocks, mutexes, **RW locks**, **semaphores**, **futex** | Stable |
| Syscalls | **36+ syscalls** — POSIX core + Wayland + drivers + AI + touch | Stable |
| AI/ML | **Tensor memory pool**, compute job dispatch, accelerator device abstraction | In-tree |
| Touch | **MT Protocol B** (10 slots), gesture engine, palm rejection, calibration | In-tree |
| OOM Killer | RSS + nice + AI-hint scoring, memory pressure callbacks | In-tree |
| Power | CPU idle governor (C-states via `hlt`/`mwait`) | In-tree |
| cgroups v2 | CPU shares, memory limits, PID limits | In-tree |
| Display | **Wayland-style Compositor** — glassmorphic UI, damage tracking | Scaffold |
| Networking | e1000, iwlwifi, TCP/UDP/ICMP/ARP/DHCP/DNS stubs | Scaffold |

## Quick Start

```bash
make setup        # Install deps (Ubuntu/WSL): nasm, gcc, xorriso, qemu, limine
make iso          # Build bootable ISO
make run          # Launch in QEMU (BIOS)
make run-uefi     # Launch in QEMU (UEFI)
make clean        # Clean build
```

**Output:** `build/yamkernel.elf` (kernel) · `build/yamkernel.iso` (bootable ISO)

## Architecture

```
+--------------------------- Ring 3 (user) ----------------------------+
|  User apps (ELF64)  |  AI inference tasks  |  POSIX-ish runtime      |
+--------------------------- SYSCALL / SYSRET -------------------------+
|  syscall_dispatch  (36+ syscalls: fork, kill, mmap, AI, touch, ...)  |
+--------------------------- Ring 0 services ---------------------------+
|  Scheduler (CFS O(log n))  |  OOM Killer  |  cgroups v2  |  Power    |
+--------------------------- Core kernel --------------------------------+
|  YamGraph  | IPC (channels, capabilities) | VFS / NET scaffolds       |
+--------------------------- Memory + CPU --------------------------------+
|  PMM (Zone-aware Cell) | VMM (CoW + Huge) | Heap (Buckets) | Slab    |
|  GDT/IDT/TSS | APIC timer | ACPI/MADT | SYSCALL MSRs | Security     |
+---------------------------- Hardware (Limine boot) --------------------+
|  CPU(s) | RAM | LAPIC/IOAPIC | PCI/USB | NIC | Display | Touch | AI  |
+------------------------------------------------------------------------+
```

## Shell Commands

```
help  top  mem  cpu  pci  graph  net  fs  uptime  date  uname  clear  reboot  shutdown
```

## Comparison

| Aspect | YamKernel | Linux | Windows NT | seL4 | Redox |
|--------|-----------|-------|------------|------|-------|
| Style | Hybrid | Monolithic | Hybrid | Microkernel | Microkernel |
| Language | C + ASM | C + ASM | C + C++ | C (proven) | Rust |
| Resource model | **YamGraph** | UID/GID + cgroups | SID + ACL | Capabilities | Capabilities |
| Memory | Zone Cell + Slab + Heap | Buddy + Slub | Pool + LFH | Static + caps | Buddy + Slab |
| Scheduler | CFS O(log n) | CFS / EEVDF | MLF | Mixed-criticality | Round-robin |
| AI Accel | ✓ (kernel framework) | ✗ (userspace) | ✗ (userspace) | ✗ | ✗ |
| Touch/Gesture | ✓ (MT Protocol B) | ✓ | ✓ | ✗ | ✗ |

See `developer.html` for deep architecture reference · See `DEBUGGING.md` for testing guide

## License

MIT
