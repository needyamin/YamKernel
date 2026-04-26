# YamOS

**A Graph-Based Adaptive Operating System**

YamOS is a modern, self-contained operating system for x86_64 powered by the **YamKernel**. It introduces a unique architecture: the **YamGraph Resource Graph**. Every system resource — processes, memory, devices, files, IPC channels — lives as a node in a live directed graph, with permissions flowing through edges as unforgeable capability tokens.

## YamOS Features

| Subsystem | YamKernel Approach |
|-----------|--------------------|
| Boot | **YamBoot** — custom pre-kernel menu (Normal / Safe Mode / Reboot) with 5-second headless timeout |
| Resource Model | **YamGraph** — live directed graph of all resources |
| Permissions | **Capability tokens** flowing through graph edges |
| CPU Topology | **Full Symmetric Multiprocessing (SMP)** — all logical cores booted and managed via LAPIC |
| Privilege | **Ring 0 / Ring 3 split** with SYSCALL/SYSRET fast path, SMAP/SMEP/UMIP/NX |
| Scheduling | **CFS-lite** — vruntime-based fair scheduler, per-task kernel stacks, APIC-timer preemption |
| Context Switch | Seamless general-purpose & **FPU/SIMD (`XSAVE`/`FXSAVE`/`AVX`)** register snapshotting |
| Sync | **Spinlocks**, **blocking mutexes**, **wait queues**, `task_sleep_ms()` |
| Memory | **Cell Allocator** (PMM, fractal quad-tree, strict power-of-4 splits) + **Slab allocator** (`kmem_cache`) + heap |
| Virtual Memory | 4-level paging with **huge-page-aware** PT walker |
| Per-CPU | **GS_BASE-backed `percpu_t`** arrays (Linux-style) with kernel/user GS swap on IRQ + SYSCALL |
| ACPI / IRQ | **ACPI MADT** parsing, **LAPIC** + **IO-APIC**, APIC timer at 100 Hz |
| Video | **Software Framebuffer** with build-time asset downscaling (`img2raw`) and hardware-halt splash animation |
| Display Server | **Wayland-style Compositor** — login screen, glassmorphic UI, damage tracking, lock-free Evdev input ring |
| GUI Apps | **Terminal**, **Calculator**, and **Web Browser** running as fully preempted isolated tasks |
| Executables | **ELF64 Loader** — dynamic Ring 3 address space creation, PT_LOAD mapping, privilege dropping |
| IPC | **Channels** — typed bidirectional graph edges |
| Terminal | **macOS-Style Bash Shell** — full History Ring and extended scancode navigation |
| Networking | **Multi-Layer Data Link** — e1000 Gigabit, Intel Wireless (wlan0), USB Bluetooth (hci0) (Scaffolding) |
| Network Protocols | **Full Stack Scaffolding** — TCP, UDP, ICMP, ARP, DHCP, DNS skeletons active |

## Building

### Prerequisites (Ubuntu/WSL)

```bash
make setup    # Installs: nasm, gcc, xorriso, qemu, limine
```

### Build & Run

```bash
make          # Build kernel ELF
make iso      # Build bootable ISO
make run      # Launch in QEMU
```

### Output

- `build/yamkernel.elf` — YamKernel binary
- `build/yamkernel.iso` — Bootable YamOS ISO (VMware, VirtualBox, bare metal)

## Project Structure

```
kernel/
├── Makefile              # Build system (recursive — auto-discovers all *.c/*.asm)
├── linker.ld             # Kernel linker script (higher-half)
├── limine.conf           # Bootloader config
├── vendor/
│   └── limine.h          # Limine boot protocol header
└── src/
    ├── boot/
    │   └── yamboot.c/h   # YamBoot — custom pre-kernel boot menu
    ├── kernel/
    │   ├── main.c        # Entry point & boot sequence
    │   ├── panic.c       # Kernel panic handler
    │   └── shell.c/h     # Interactive REPL shell
    ├── cpu/
    │   ├── gdt.c/h       # GDT + TSS (SYSRET-compatible user segments)
    │   ├── idt.c/h       # Interrupt Descriptor Table
    │   ├── cpuid.c/h     # CPU feature detection
    │   ├── msr.h         # MSR / CR / CPUID inline helpers
    │   ├── security.c/h  # Enable NX, SMEP, SMAP, UMIP, WP, FSGSBASE
    │   ├── acpi.c/h      # RSDP/XSDT/MADT parser (CPU + IO-APIC topology)
    │   ├── apic.c/h      # LAPIC + IO-APIC + APIC timer (PIT-calibrated)
    │   ├── percpu.c/h    # Per-CPU data via GS_BASE
    │   ├── smp.c/h       # CPU enumeration (AP boot pending)
    │   ├── syscall.c/h   # SYSCALL/SYSRET MSR setup + dispatcher
    │   ├── syscall.asm   # SYSCALL entry stub (swapgs, kernel stack switch)
    │   └── isr.asm       # Interrupt stubs with ring-3 swapgs handling
    ├── sched/
    │   ├── sched.c/h     # CFS-lite scheduler (vruntime, per-task kstack)
    │   ├── wait.c/h      # Wait queues, task_sleep_ms, blocking mutex
    │   ├── context.asm   # Kernel context switch + task trampoline
    │   ├── enter_user.asm# iretq into Ring 3 (user CS/SS, IF=1, regs cleared)
    │   ├── user.c        # Demo Ring 3 task loader (maps code+stack pages)
    │   ├── user_demo.asm # Tiny PIC ring-3 program (SYS_WRITE + SYS_YIELD)
    │   └── demo.c        # Kernel-thread demo (sleep_ms + mutex print)
    ├── mem/
    │   ├── pmm.c/h       # Cell Allocator (physical memory, fractal quad-tree)
    │   ├── vmm.c/h       # Virtual memory (4-level paging, huge-page safe)
    │   ├── slab.c/h      # Slab allocator (kmem_cache_create/alloc/free)
    │   └── heap.c/h      # Kernel heap (kmalloc/kfree)
    ├── nexus/
    │   ├── graph.c/h     # YamGraph — core resource graph
    │   ├── capability.c/h # Capability token manager
    │   └── channel.c/h   # IPC channels
    ├── drivers/
    │   ├── bluetooth/    # Bluetooth HCI stub
    │   ├── bus/          # PCI enumeration + USB/I2C/SPI APIs
    │   ├── input/        # PS/2 keyboard + mouse
    │   ├── net/          # e1000 NIC, iwlwifi stub
    │   ├── serial/       # COM1 serial output
    │   ├── timer/        # PIT system tick + RTC
    │   └── video/        # Framebuffer text rendering
    ├── wayland/              # YamOS Desktop Environment (Compositor & Clients)
    ├── net/              # TCP / UDP / ICMP / ARP / DHCP / DNS skeletons
    ├── ipc/              # IPC mechanisms scaffolding
    ├── fs/               # VFS (FAT32 / ext4 / NTFS scaffolding) & ELF Loader
    ├── lib/
    │   ├── kprintf.c/h   # Kernel printf
    │   ├── spinlock.h    # IRQ-safe spinlock primitive
    │   └── string.c/h    # String functions (optimized rep movsb / stosb)
    └── include/nexus/
        ├── types.h       # Core types & port I/O
        └── panic.h       # Panic interface
```

## Using the ISO

### QEMU
```bash
qemu-system-x86_64 -cdrom build/yamkernel.iso -m 256M -serial stdio
```

### VirtualBox
1. Create a new VM (Type: Other, Version: Other/Unknown 64-bit)
2. Attach `yamkernel.iso` as a CD/DVD
3. Boot

### VMware
1. Create a new VM (Guest OS: Other 64-bit)
2. Attach `yamkernel.iso` as CD/DVD
3. Boot

### Proxmox / KVM
1. Upload `yamkernel.iso` to Proxmox ISO storage.
2. Create VM (OS: Other, Machine: q35, BIOS: SeaBIOS).
3. **IMPORTANT**: For mouse support, go to Hardware -> double click "Pointer" (or "USB Tablet") and set **Use tablet for pointer: No**. YamKernel relies on PS/2 Mouse emulation.

### Bare Metal (USB)
```bash
sudo dd if=build/yamkernel.iso of=/dev/sdX bs=4M status=progress
```

## Boot Flow

```
Limine bootloader  →  kernel_main()
   ↓
serial_init  →  fb_init  →  YamBoot menu
                                  ↓
        ┌───── [1] Normal ───────┤
        │                        ├──── [3] Reboot (8042 reset)
        └───── [2] Safe Mode ────┘
                ↓
   GDT / IDT / CPUID / Security (NX·SMEP·SMAP·UMIP·WP)
        ↓
   VMM / PMM / Heap / Slab
        ↓
   ACPI (MADT) → LAPIC + IO-APIC → percpu_init → SMP enumerate → syscall_init
        ↓
   YamGraph init + self-tests
        ↓
   PIT 100Hz (boot tick)  →  Keyboard
        ↓
   (Normal only) PCI / USB / I2C / SPI / VFS / IPC / NET / Mouse
        ↓
   sched_init → spawn demo + ring-3 demo → APIC timer 100 Hz → sched_enable
        ↓
   Splash Screen Animation (Hardware Halt 0% CPU sleep)
        ↓
   (Normal) Wayland Compositor (Task #0)  →  Login Screen  →  Desktop
        ↓
   (Safe) shell_start()  (runs as task #0; CFS-lite preempts via APIC timer)
        ↓
   yam@kernel ~ %
```

## Interactive Shell Commands

Once booted, type commands at the `yam@kernel ~ %` prompt:

| Command | Description |
|---------|-------------|
| `help` | Show full command reference |
| `top` | Live btop-style dashboard (CPU / MEM / NET / SYS) |
| `mem` | Cell Allocator state and usage bar |
| `cpu` | CPU vendor, brand, and feature flags |
| `pci` / `lspci` | Enumerated PCI devices |
| `graph` | YamGraph nodes and edges with permissions |
| `net` / `ifconfig` | Network interface and protocol status |
| `ipc` | IPC mechanism status |
| `fs` | Mounted volumes and supported filesystems |
| `uptime` | System running time |
| `date` | Current RTC date/time |
| `uname` / `ver` / `version` | Kernel version |
| `whoami` | Current shell user |
| `echo <text>` | Print text |
| `clear` | Clear the framebuffer |
| `reboot` / `restart` | Restart machine (PS/2 + ACPI + triple-fault fallback) |
| `shutdown` | ACPI shutdown (QEMU / VMware / VirtualBox / Bochs) |

History navigation: ↑ / ↓ arrow keys cycle through up to 15 previous commands.

## Architecture

### YamBoot
The YamOS boot manager. A custom pre-kernel boot stage that runs after Limine but before the rest of `kernel_main`. Features a 5-second auto-boot timeout (via hardware port `0x80` delays) for headless virtualization support. Polls the PS/2 keyboard directly and lets the user pick:
- **Normal Boot** — bring up the full YamOS environment (GUI + Apps)
- **Safe Mode** — only PIT + keyboard + shell (minimal debug environment)
- **Reboot** — 8042 keyboard-controller reset

### YamGraph
The kernel's core is a directed graph where:
- **Nodes** represent resources (tasks, memory cells, devices, files)
- **Edges** carry capability tokens with typed permissions
- Operations like scheduling and IPC query the graph topology
- Revoking a capability cascades through the graph

### Cell Allocator
Physical memory uses YamKernel's novel fractal quad-tree algorithm:
- Bootloader usable regions are partitioned strictly into powers of 4 (e.g. 4096, 16384, 65536) to prevent non-page-aligned subdivisions.
- The root cells represent physical memory chunks.
- Split: each cell divides into 4 equal children, guaranteeing perfect page alignment.
- Merge: when all 4 siblings are free, they coalesce.
- Each cell tracks its owner via YamGraph node reference.

### Virtual Memory
- 4-level x86_64 paging (PML4 → PDPT → PD → PT)
- Page-table walker is **huge-page aware**: refuses to descend into existing 2 MB / 1 GB entries (e.g. Limine's HHDM), so MMIO mappings on top of pre-mapped regions can't corrupt page tables.
- TLB invalidated via `mov cr3, cr3` after MMIO mappings.

### Slab Allocator
- `kmem_cache_create(name, size, align)` → fixed-size object cache.
- One PMM page per slab; objects threaded onto a per-cache freelist via their first 8 bytes — alloc/free are O(1).
- IRQ-safe via per-cache spinlock; tracks alloc/free counters.
- Used by the OS for `task_t` and graph node allocations.

### Scheduler (CFS-lite)
- Single ready list; pick the task with the **lowest `vruntime`** each switch.
- `vruntime += 1024 / weight` per timer tick (weights `8 / 4 / 2 / 1` by priority), so high-priority/heavy-weight tasks get more CPU.
- New tasks **inherit the minimum vruntime** of the queue so they don't immediately monopolize.
- **Per-task kernel stack**: `switch_to()` rebinds `TSS.rsp0` and `percpu.kernel_rsp` so IRQs from Ring 3 and SYSCALL entry both land on the current task's own kernel stack.
- **Sleeping list** scanned every tick — `task_sleep_ms()` blocks instead of busy-waiting.
- **Wait queues + blocking `mutex_t`** (atomic xchg fast path, queue slow path), with the lost-wakeup race avoided by holding `IF=0` across enqueue+yield.

### Ring 3 / Syscalls
- `STAR / LSTAR / SFMASK / EFER.SCE` configured for SYSCALL/SYSRET.
- GDT layout matches Linux convention (CS=0x23, SS=0x1B for ring 3).
- Per-CPU `kernel_rsp` (gs:[40]) holds the syscall stack; `user_rsp_save` (gs:[32]) stashes the user RSP.
- ISR stubs auto-`swapgs` on entry/exit when interrupted from Ring 3.
- `sys_write` is **SMAP-aware** — wraps user-buffer reads with `STAC` / `CLAC`.

### ELF64 Loader
- Parses standard ELF64 executables dynamically from memory/disk.
- Creates fully isolated `pml4` page tables for user processes by cloning the kernel's top-half memory.
- Maps `PT_LOAD` segments into the lower-half user address space.
- Allocates an isolated 16KB user stack.
- Uses `iretq` to safely transition CPU privilege from Ring 0 to Ring 3 execution at the ELF entry point.

### Wayland Compositor & GUI
- A custom display server managing multiple Ring 3 isolated graphical clients.
- **Glassmorphism**: Features real-time alpha blending and "frosted glass" effects for the Login Screen, Dock, and Window Titlebars.
- **Input Routing**: Uses **Evdev** ring buffers for lock-free routing of PS/2 mouse coordinates and clicks to focused surfaces.
- **Damage Tracking (Shadow Buffer)**: Compares rendering output to a RAM shadow-buffer and only writes changed pixels to the Uncacheable (UC) MMIO framebuffer using `rep movsb`. This completely eliminates KVM VM-Exit storms, ensuring < 5% idle CPU usage on Proxmox/Hypervisors.
- **Features**: Multi-window management, floating dock, real-time clock, and a secure login screen (`root`/`password`).

### Splash Screen
- Branded boot animation showing the YamKernel logo and a progress spinner.
- **Efficiency**: The animation loop uses the `hlt` instruction to sleep the CPU until the next PIT interrupt, achieving nearly 0% CPU usage during the transition to the GUI.


## Build Command 
```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/laragon/www/kernel && make clean && make iso"
```

## License

MIT



