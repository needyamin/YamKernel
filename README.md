# YamKernel

**A Graph-Based Adaptive Operating System Kernel**

YamKernel is a completely novel OS kernel for x86_64 that introduces a unique architecture: the **YamGraph Resource Graph**. Every system resource — processes, memory, devices, files, IPC channels — lives as a node in a live directed graph, with permissions flowing through edges as unforgeable capability tokens.

## YamKernel Features

| Subsystem | YamKernel Approach |
|-----------|--------------------|
| Boot | **YamBoot** — custom pre-kernel menu (Normal / Safe Mode / Reboot) |
| Resource Model | **YamGraph** — live directed graph of all resources |
| Permissions | **Capability tokens** flowing through graph edges |
| Scheduling | **Flow Scheduler** — graph topology-based priority |
| Memory | **Cell Allocator** — fractal quad-tree allocation |
| Virtual Memory | 4-level paging with **huge-page-aware** PT walker |
| IPC | **Channels** — typed bidirectional graph edges |
| Terminal | **macOS-Style Bash Shell** — full History Ring and extended scancode navigation |
| Networking | **Multi-Layer Data Link** — e1000 Gigabit, Intel Wireless (wlan0), USB Bluetooth (hci0) |
| Network Protocols | **Full Stack Scaffolding** — TCP, UDP, ICMP, ARP, DHCP, DNS |

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

- `build/yamkernel.elf` — Kernel binary
- `build/yamkernel.iso` — Bootable ISO (VMware, VirtualBox, bare metal)

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
    │   ├── gdt.c/h       # Global Descriptor Table + TSS
    │   ├── idt.c/h       # Interrupt Descriptor Table
    │   ├── cpuid.c/h     # CPU feature detection
    │   └── isr.asm       # Interrupt stubs (x86_64)
    ├── mem/
    │   ├── pmm.c/h       # Cell Allocator (physical memory)
    │   ├── vmm.c/h       # Virtual memory (4-level paging, huge-page safe)
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
    ├── net/              # TCP / UDP / ICMP / ARP / DHCP / DNS skeletons
    ├── ipc/              # IPC mechanisms scaffolding
    ├── fs/               # VFS (FAT32 / ext4 / NTFS scaffolding)
    ├── lib/
    │   ├── kprintf.c/h   # Kernel printf
    │   └── string.c/h    # String functions
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
   GDT / IDT / CPUID  →  VMM / PMM / Heap
        ↓
   YamGraph init + self-tests
        ↓
   PIT 100Hz  →  Keyboard
        ↓
   (Normal only) PCI / USB / I2C / SPI / VFS / IPC / NET / Mouse
        ↓
   shell_start()  →  yam@kernel ~ %
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
Custom pre-kernel boot stage that runs after Limine but before the rest of `kernel_main`. Polls the PS/2 keyboard directly (no IDT yet) and lets the user pick:
- **Normal Boot** — bring up every subsystem
- **Safe Mode** — only PIT + keyboard + shell (skip PCI/USB/VFS/IPC/NET/Mouse)
- **Reboot** — 8042 keyboard-controller reset

### YamGraph
The kernel's core is a directed graph where:
- **Nodes** represent resources (tasks, memory cells, devices, files)
- **Edges** carry capability tokens with typed permissions
- Operations like scheduling and IPC query the graph topology
- Revoking a capability cascades through the graph

### Cell Allocator
Physical memory uses YamKernel's original fractal quad-tree algorithm:
- The root cell = entire physical memory
- Split: each cell divides into 4 equal children
- Merge: when all 4 siblings are free, they coalesce
- Each cell tracks its owner via YamGraph node reference

### Virtual Memory
- 4-level x86_64 paging (PML4 → PDPT → PD → PT)
- Page-table walker is **huge-page aware**: refuses to descend into existing 2 MB / 1 GB entries (e.g. Limine's HHDM), so MMIO mappings on top of pre-mapped regions can't corrupt page tables.
- TLB invalidated via `mov cr3, cr3` after MMIO mappings.

## License

MIT
