# YamKernel Debugging & Testing Guide

This repository contains a structured debugging and testing system for rapid kernel development.

---

## 📂 Directory Structure

```
kernel/
├── scripts/
│   ├── test.sh          # WSL/Linux rapid test script
│   └── test.ps1         # Windows PowerShell rapid test script
├── .vscode/
│   ├── launch.json      # VS Code GDB debugger config
│   └── tasks.json       # VS Code build/launch tasks
├── src/lib/
│   ├── kdebug.h         # Debug logging macros (KTRACE, KDBG, KINFO, KWARN, KERR)
│   └── kdebug.c         # Serial-only debug output implementation
├── Makefile             # Build system with debug targets
└── DEBUGGING.md         # This file
```

---

## 🚀 Rapid Testing (The "Edit → Build → Run" Loop)

After making code changes, use the test scripts:

### Windows (PowerShell)
```powershell
.\scripts\test.ps1              # Build + run (graphical QEMU window)
.\scripts\test.ps1 serial       # Build + run with serial log file
.\scripts\test.ps1 headless     # Build + serial output on terminal
.\scripts\test.ps1 debug        # Build + GDB attach mode
```

### WSL / Linux
```bash
./scripts/test.sh               # Build + run (graphical QEMU window)
./scripts/test.sh serial        # Build + run with serial log file
./scripts/test.sh headless      # Build + serial output on terminal
./scripts/test.sh debug         # Build + GDB attach mode
```

---

## 🔍 Kernel Debug Logging (`kdebug`)

A structured, serial-only logging system built into the kernel.  
Logs **always** go to the COM1 serial port — they work even when the framebuffer is off, corrupted, or not yet initialized.

### Log Levels

| Macro | Level | Use For |
|-------|-------|---------|
| `KTRACE(tag, fmt, ...)` | 0 | Function entry/exit, ultra-verbose |
| `KDBG(tag, fmt, ...)`   | 1 | Internal state dumps |
| `KINFO(tag, fmt, ...)`  | 2 | Normal boot progress |
| `KWARN(tag, fmt, ...)`  | 3 | Recoverable issues |
| `KERR(tag, fmt, ...)`   | 4 | Fatal/critical errors |

### Example Usage

```c
#include "../lib/kdebug.h"

void my_driver_init(void) {
    KINFO("MYDRV", "Initializing driver v%d", 1);
    KTRACE("MYDRV", "register base = %p", base_addr);
    
    if (!success) {
        KERR("MYDRV", "Init failed! status=%x", status);
    }
}
```

### Hex Dump

```c
kdebug_hexdump("MYDRV", buffer, 64);
```

### Compile-Time Filtering

Set `KDEBUG_LEVEL` before including `kdebug.h` to filter messages:

```c
#define KDEBUG_LEVEL KDEBUG_WARN  // Only show WARN and ERROR
#include "../lib/kdebug.h"
```

---

## 📡 Serial Output Methods

### Method 1: Serial Log File
```bash
make run-serial
# Then in another terminal:
tail -f build/serial.log
```

### Method 2: Headless (Serial on Terminal)
```bash
make run-serial-only
# All serial output appears directly. Press Ctrl+A then X to quit.
```

### Method 3: Proxmox
Add a serial port to your VM (Hardware → Serial Port 0), then view via:
```
qm terminal <VMID>
```

---

## 🛠 GDB Debugging

For stepping through code and inspecting crashes:

### 1. Launch Debug Server
```bash
make debug
```
*QEMU starts frozen, listening on `localhost:1234`.*

### 2. Attach GDB (in another terminal)
```bash
gdb build/yamkernel.elf
(gdb) target remote localhost:1234
(gdb) break kernel_main
(gdb) continue
```

### 3. VS Code (Press F5)
The `.vscode/launch.json` is pre-configured to build, launch QEMU in debug mode, and attach the visual debugger automatically.

---

## 🧪 Makefile Targets

| Target | Description |
|--------|-------------|
| `make iso` | Build the bootable ISO |
| `make run` | Build + launch in QEMU (graphical) |
| `make run-serial` | Build + launch with serial log file |
| `make run-serial-only` | Build + headless (serial on terminal) |
| `make debug` | Build + launch frozen for GDB attach |
| `make run-uefi` | Build + launch with UEFI firmware |
| `make clean` | Remove all build artifacts |
| `make setup` | Install build dependencies (Ubuntu/WSL) |
