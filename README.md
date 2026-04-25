# YamKernel

**A Graph-Based Adaptive Operating System Kernel**

YamKernel is a completely novel OS kernel for x86_64 that introduces a unique architecture: the **YamGraph Resource Graph**. Every system resource — processes, memory, devices, files, IPC channels — lives as a node in a live directed graph, with permissions flowing through edges as unforgeable capability tokens.

## What Makes YamKernel Different

| Feature | Linux | YamKernel |
|---------|-------|-----------|
| Resource Model | Flat PID + fd tables | Directed graph (YamGraph) |
| Permissions | UID/GID + ACLs | Capability tokens on graph edges |
| Scheduling | CFS fairness tree | Flow Scheduler (graph topology) |
| Memory | Buddy allocator | Cell Allocator (fractal quad-tree) |
| IPC | Pipes, sockets | Typed bidirectional Channels |
| Filesystem | VFS tree | Graph-structured NexusFS |

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
├── Makefile              # Build system
├── linker.ld             # Kernel linker script (higher-half)
├── limine.conf           # Bootloader config
├── vendor/
│   └── limine.h          # Limine boot protocol
└── src/
    ├── kernel/
    │   ├── main.c        # Entry point & boot sequence
    │   └── panic.c       # Kernel panic handler
    ├── cpu/
    │   ├── gdt.c/h       # Global Descriptor Table + TSS
    │   ├── idt.c/h       # Interrupt Descriptor Table
    │   └── isr.asm       # Interrupt stubs (x86_64)
    ├── mem/
    │   ├── pmm.c/h       # Cell Allocator (physical memory)
    │   ├── vmm.c/h       # Virtual memory (4-level paging)
    │   └── heap.c/h      # Kernel heap (kmalloc/kfree)
    ├── nexus/
    │   ├── graph.c/h     # YamGraph — core resource graph
    │   ├── capability.c/h # Capability token manager
    │   └── channel.c/h   # IPC channels
    ├── drivers/
    │   ├── serial.c/h    # COM1 serial output
    │   └── framebuffer.c/h # Framebuffer text rendering
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

## Architecture

### YamGraph
The kernel's core is a directed graph where:
- **Nodes** represent resources (tasks, memory cells, devices, files)
- **Edges** carry capability tokens with typed permissions
- Operations like scheduling and IPC query the graph topology
- Revoking a capability cascades through the graph

### Cell Allocator
Physical memory uses a fractal quad-tree instead of Linux's buddy system:
- The root cell = entire physical memory
- Split: each cell divides into 4 equal children
- Merge: when all 4 siblings are free, they coalesce
- Each cell tracks its owner via YamGraph node reference

## License

MIT
