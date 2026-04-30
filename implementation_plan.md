# YamOS v0.4.0 — Full Implementation Plan

## Goal

Implement all 7 Critical Features (C1–C7) from the future roadmap, restructure the `src/os` application layer to match a proper OS hierarchy (Windows/Linux-style), and upgrade the desktop compositor to a **macOS-style** GUI.

---

## Current State Assessment

| Component | Status |
|---|---|
| C1 — SMP | ✅ **Done** — Limine SMP, AP trampoline, all cores running |
| C2 — FAT32 / Filesystem | ❌ **Stub only** — `vfs.c` is placeholder, no real driver |
| C3 — USB / XHCI / HID | ❌ **Stub only** — `usb_init()` prints a line and returns |
| C4 — TCP/IP Stack | ❌ **Stub only** — `net_tcp_init()` prints and returns |
| C5 — POSIX libc | ⚠️ **Partial** — `printf`, `malloc`, `string.h` exist, no `fopen`/`fread` |
| C6 — ELF exec() | ✅ **Done** — ELF loader loads from memory module, runs Ring 3 |
| C7 — Virtual Consoles | ❌ **Missing** — Only a single Wayland compositor, no tty sessions |
| Desktop GUI | ⚠️ **Partial** — Custom compositor with dock, needs macOS design |
| OS Structure | ⚠️ **Flat** — `src/os/` has apps/services lumped together |

---

## Open Questions

> [!IMPORTANT]
> **C1 — SMP**: The existing Limine SMP implementation already boots all AP cores and runs the scheduler. The "INIT-SIPI-SIPI" sequence is handled by Limine. Should we **add a bare-metal manual INIT-SIPI-SIPI path** (for when Limine is not used), or is the current Limine-based boot sufficient for your use case?

> [!IMPORTANT]
> **C4 — TCP/IP Detail Level**: Implementing a *full* production TCP/IP stack (RFC 793 with retransmission, windowing, congestion control) is 4–6 weeks of work. Should we implement:
> - **Option A**: Full production TCP (SYN/SYN-ACK/ACK, retransmit, sliding window, FIN/RST) — most correct
> - **Option B**: Simplified TCP (3-way handshake, data transfer, no retransmit) — usable for basic HTTP
> - **Option C**: Replace TCP with `lwIP` port (fastest, but external dependency)

> [!IMPORTANT]
> **C7 — Virtual Console Mode**: Alt+F1..F6 virtual consoles require either (a) the Wayland compositor stays running in GUI mode OR (b) you drop to a text framebuffer mode. Should virtual consoles co-exist with the Wayland GUI (like Linux VT switching) or replace the GUI entirely?

> [!IMPORTANT]
> **macOS Desktop Style**: The current compositor has a floating bottom dock (already macOS-like). The upgrade to full macOS style would add: top menu bar, spotlight search, native window shadows, Mission Control (app switcher with Cmd+Tab), and an animated Launchpad. Confirm scope.

---

## Proposed Changes

### Architecture Overview (New `src/os` Structure)

The `src/os` directory will be restructured to match a proper OS hierarchy:

```
src/os/
├── apps/               (user-space apps — unchanged, compiled as ELF)
│   ├── authd.c
│   ├── calculator.c
│   ├── browser.c
│   ├── terminal.c
│   └── ...
├── bin/                [NEW] system binaries (shell, init, login)
│   ├── init.c          (PID 1 — spawns services, manages runlevel)
│   ├── login.c         (login program, reads /etc/passwd)
│   └── sh.c            (basic POSIX-like shell)
├── etc/                [NEW] config files (virtual, embedded in initrd)
│   ├── passwd.h        (default user table — root, guest)
│   └── fstab.h         (mount table)
├── dev/                [NEW] device node registry
│   └── devfs.c         (device filesystem — /dev/tty0..5, /dev/null)
├── proc/               [NEW] process info filesystem
│   └── procfs.c        (stubs for /proc/cpuinfo, /proc/meminfo)
├── lib/                (user-space libraries — unchanged)
│   ├── libc/           (stdio, stdlib, string — to be expanded)
│   ├── libgui/
│   └── libyam/
├── services/           (kernel-mode services — unchanged)
│   ├── compositor/     (macOS-style desktop — to be upgraded)
│   └── shell/
└── drivers/            (user-space driver processes — unchanged)
```

---

### C1 — SMP Enhancement (INIT-SIPI-SIPI Manual Path)

The existing Limine SMP is complete. We will **add a manual INIT-SIPI-SIPI fallback** for non-Limine boots.

#### [MODIFY] [smp.c](file:///c:/laragon/www/YamOS/src/cpu/smp.c)
- Add `smp_init_manual()` using LAPIC `ICR` register to send INIT → SIPI → SIPI to each AP
- Keep Limine path as primary (used when `smp_resp != NULL`)
- Add IPI (Inter-Processor Interrupt) helpers: `smp_send_ipi(cpu_id, vector)` for cross-CPU wakeup

#### [NEW] [smp_trampoline.asm](file:///c:/laragon/www/YamOS/src/cpu/smp_trampoline.asm)
- 16-bit real mode trampoline at physical address 0x8000
- Transitions AP from real mode → protected mode → long mode
- Used only by the manual INIT-SIPI-SIPI path

---

### C2 — Real Filesystem (FAT32 + initrd)

This is the largest missing piece. We implement a full FAT32 read/write driver.

#### [NEW] [fat32.c](file:///c:/laragon/www/YamOS/src/fs/fat32.c) + [fat32.h](file:///c:/laragon/www/YamOS/src/fs/fat32.h)
- Parse BPB (BIOS Parameter Block) from FAT32 volume
- FAT table read/write (cluster chain traversal)
- Directory entry read/write (8.3 names + LFN)
- `fat32_read_file()`, `fat32_write_file()`, `fat32_readdir()`
- `fat32_mount(void *disk_data, usize size)` — mounts from memory (ramdisk)

#### [NEW] [initrd.c](file:///c:/laragon/www/YamOS/src/fs/initrd.c) + [initrd.h](file:///c:/laragon/www/YamOS/src/fs/initrd.h)
- Simple in-memory filesystem: a flat table of `{name, data, size}` entries
- Stores `/etc/passwd`, `/etc/fstab`, initial ELF binaries
- `initrd_lookup(name)` → returns pointer to file data + size

#### [MODIFY] [vfs.c](file:///c:/laragon/www/YamOS/src/fs/vfs.c)
- Replace stubs with real VFS layer
- `vfs_mount(source, target, type)` — registers a mount point
- `sys_open()` → resolves path → finds mount → calls fs driver `open()`
- `sys_read()`, `sys_write()`, `sys_close()`, `sys_stat()`, `sys_readdir()`
- Mount table: `vfs_mount_t mounts[16]`
- File descriptor routing through `file_ops_t` vtable

#### [NEW] [devfs.c](file:///c:/laragon/www/YamOS/src/os/dev/devfs.c)
- Virtual `/dev` filesystem
- `/dev/null` — reads return 0, writes discard
- `/dev/tty0..5` — virtual console character devices
- `/dev/fb0` — framebuffer device
- `/dev/keyboard` — keyboard event stream

---

### C3 — USB Stack (XHCI + HID)

#### [NEW] [xhci.c](file:///c:/laragon/www/YamOS/src/drivers/usb/xhci.c) + [xhci.h](file:///c:/laragon/www/YamOS/src/drivers/usb/xhci.h)
- XHCI Controller initialization via PCI MMIO BAR
- Host Controller Reset sequence
- Port reset and USB3/USB2 device enumeration
- Transfer Ring management (Command Ring, Event Ring, Transfer Rings)
- `xhci_submit_control()` — submit control transfers (GET_DESCRIPTOR, SET_ADDRESS)
- `xhci_submit_interrupt()` — interrupt endpoints for HID polling

#### [NEW] [usb_core.c](file:///c:/laragon/www/YamOS/src/drivers/usb/usb_core.c) + [usb_core.h](file:///c:/laragon/www/YamOS/src/drivers/usb/usb_core.h)
- USB device registry: `usb_device_t` with `vendor_id`, `product_id`, `class`
- Descriptor parsing: Device, Configuration, Interface, Endpoint
- Class driver dispatch: USB_CLASS_HID → `hid_probe()`

#### [NEW] [hid.c](file:///c:/laragon/www/YamOS/src/drivers/usb/hid.c) + [hid.h](file:///c:/laragon/www/YamOS/src/drivers/usb/hid.h)
- USB HID class driver
- HID Report Descriptor parsing (boot protocol fallback)
- Keyboard report → `evdev_push_event(EV_KEY, keycode, value)`
- Mouse report → `evdev_push_event(EV_REL, REL_X/Y, delta)` 
- Touch report → `evdev_push_event(EV_ABS, ...)`

#### [MODIFY] [api.c](file:///c:/laragon/www/YamOS/src/drivers/bus/api.c)
- Replace `usb_init()` stub with real `xhci_probe_all_pci_controllers()`

---

### C4 — Working TCP/IP Stack

Implementing **Option A** (Full production TCP) with a clean layered design.

#### [MODIFY] [arp.c](file:///c:/laragon/www/YamOS/src/net/arp.c)
- ARP request/reply state machine
- ARP cache: `arp_cache_t cache[64]` — IP → MAC mapping
- `arp_lookup(ip, mac_out)` — cache hit or send request and wait
- `arp_handle_packet(buf, len)` — process incoming ARP

#### [MODIFY] [ip.c](file:///c:/laragon/www/YamOS/src/net/ip.c)  
- IPv4 header build/parse
- IP checksum calculation
- `ip_send(dst_ip, proto, payload, len)` → ARP lookup → Ethernet send
- `ip_receive(pkt, len)` → demux to TCP/UDP/ICMP

#### [MODIFY] [icmp.c](file:///c:/laragon/www/YamOS/src/net/icmp.c)
- ICMP echo request/reply (ping responder + sender)
- `icmp_ping(ip, seq, timeout_ms)` — returns RTT or -1

#### [MODIFY] [udp.c](file:///c:/laragon/www/YamOS/src/net/udp.c)
- UDP socket: bind, sendto, recvfrom
- Port multiplexing: `udp_socket_t sockets[64]`
- `sys_socket()`, `sys_bind()`, `sys_sendto()`, `sys_recvfrom()` syscall handlers

#### [MODIFY] [tcp.c](file:///c:/laragon/www/YamOS/src/net/tcp.c)
- Full TCP state machine: CLOSED → SYN_SENT → ESTABLISHED → FIN_WAIT → CLOSE_WAIT → CLOSED
- 3-way handshake (SYN, SYN-ACK, ACK)
- Data transfer with sequence/acknowledgment numbers
- Retransmission timer (simple exponential backoff)
- Sliding window (basic receive window advertisement)
- FIN/RST handling
- `tcp_socket_t sockets[32]` — port to socket mapping
- `sys_connect()`, `sys_listen()`, `sys_accept()`, `sys_send()`, `sys_recv()` syscall handlers

#### [MODIFY] [dhcp.c](file:///c:/laragon/www/YamOS/src/net/dhcp.c)
- DHCP DISCOVER → OFFER → REQUEST → ACK sequence
- Configures `net_interface.ip_addr`, `gateway`, `dns`
- `dhcp_start(interface)` — spawns DHCP negotiation task

#### [MODIFY] [dns.c](file:///c:/laragon/www/YamOS/src/net/dns.c)
- DNS query builder and response parser
- `dns_resolve(hostname, ip_out)` — A record lookup via UDP port 53

#### [NEW] [net_socket.h](file:///c:/laragon/www/YamOS/src/net/net_socket.h)
- POSIX-like socket API types: `AF_INET`, `SOCK_STREAM`, `SOCK_DGRAM`
- `struct sockaddr_in` — compatible with POSIX naming

#### [MODIFY] [e1000.c](file:///c:/laragon/www/YamOS/src/drivers/net/e1000.c)
- Complete TX/RX ring buffer implementation (was partial)
- Interrupt-driven RX: on packet receive → `ip_receive()`
- `e1000_send(buf, len)` — queues packet into TX ring

---

### C5 — POSIX libc Enhancement

#### [MODIFY] [stdio.c](file:///c:/laragon/www/YamOS/src/os/lib/libc/stdio.c)
- Add `fopen(path, mode)` → `sys_open()` → returns `FILE *`
- Add `fread(buf, size, n, fp)` → `sys_read()`
- Add `fwrite(buf, size, n, fp)` → `sys_write()`
- Add `fclose(fp)` → `sys_close()`
- Add `fgets(buf, n, fp)` — line-buffered read
- Add `fprintf(fp, fmt, ...)`, `fputs(str, fp)`
- Improve `printf` to support `%ld`, `%lu`, `%lx`, `%05d` (width/precision)

#### [NEW] [unistd.c](file:///c:/laragon/www/YamOS/src/os/lib/libc/unistd.c) + [unistd.h](file:///c:/laragon/www/YamOS/src/os/lib/libc/unistd.h)
- `read(fd, buf, count)`, `write(fd, buf, count)`, `close(fd)`
- `getpid()`, `getppid()`, `sleep(seconds)`, `usleep(usec)`
- `exec(path, argv, envp)` → `sys_exec()` syscall

#### [NEW] [errno.h](file:///c:/laragon/www/YamOS/src/os/lib/libc/errno.h)
- POSIX error codes: `ENOENT`, `ENOMEM`, `EINVAL`, `EBADF`, `EACCES`, etc.
- Thread-local `errno` (per-task via `task->errno`)

#### [MODIFY] [stdlib.c](file:///c:/laragon/www/YamOS/src/os/lib/libc/stdlib.c)
- Add `calloc(n, size)`, `realloc(ptr, newsize)`
- Add `atoi()`, `atol()`, `strtol()`, `strtoul()`
- Add `qsort()`, `bsearch()`
- Add `exit(code)` → `sys_exit()` syscall

#### [NEW] [ctype.h](file:///c:/laragon/www/YamOS/src/os/lib/libc/ctype.h)
- `isalpha()`, `isdigit()`, `isspace()`, `toupper()`, `tolower()`

#### [NEW] [sys/socket.h](file:///c:/laragon/www/YamOS/src/os/lib/libc/sys/socket.h)
- POSIX socket API for user-space networking apps
- `socket()`, `bind()`, `connect()`, `listen()`, `accept()`, `send()`, `recv()`
- `struct sockaddr_in`, `htons()`, `htonl()`, `inet_addr()`

---

### C6 — Enhanced ELF exec() + Process Hierarchy

The basic `elf_load()` exists. We need proper `exec()` with argument passing and a process tree.

#### [MODIFY] [elf.c](file:///c:/laragon/www/YamOS/src/fs/elf.c)
- Add `elf_exec(path, argv[], envp[])` — load ELF from VFS path (not just memory)
- Push `argc`, `argv` array, and `envp` onto user stack (System V x86_64 ABI)
- Map `argv` strings into user address space
- Add `sys_exec()` syscall handler

#### [MODIFY] [sched.c](file:///c:/laragon/www/YamOS/src/sched/sched.c)
- Add `task->ppid` (parent PID)
- Add `sys_fork()` — copy-on-write process duplication
- Add `sys_wait()` / `sys_waitpid()` — block parent until child exits
- Add `sys_exit()` — clean up process, notify parent via `SIGCHLD`

#### [NEW] [os/bin/init.c](file:///c:/laragon/www/YamOS/src/os/bin/init.c)
- PID 1: the init process
- Spawns login/authd, mounts filesystems, sets up /dev
- Simple service management: restart crashed daemons

---

### C7 — Virtual Console (Alt+F1..F6)

#### [NEW] [vtty.c](file:///c:/laragon/www/YamOS/src/os/dev/vtty.c) + [vtty.h](file:///c:/laragon/www/YamOS/src/os/dev/vtty.h)
- `vtty_t` struct: `{u8 *framebuffer, u32 width, u32 height, cursor_x, cursor_y, scrollback[...], input_ring[...]}`
- 6 virtual terminals: `vtty_t g_vttys[6]`
- `vtty_write(tty_id, buf, len)` — write text to a VT's buffer
- `vtty_scroll_up(tty_id)` — shift scrollback
- `vtty_switch(tty_id)` — switch active VT, re-render to framebuffer
- `vtty_input_char(c)` — push keystroke to active VT's input ring
- VT ANSI escape sequences: `\033[H` (home), `\033[2J` (clear), `\033[1;33m` (color)

#### [MODIFY] [compositor.c](file:///c:/laragon/www/YamOS/src/os/services/compositor/compositor.c)
- Add `Alt+Fx` hotkey handler: `evdev_event.code == KEY_F1..F6 && alt_held` → `vtty_switch(n-1)`
- When in VT mode: compositor is suspended, `vtty_render()` writes directly to framebuffer
- When in GUI mode: VTs run in background (accumulate scrollback but don't render)

#### [MODIFY] [keyboard.c](file:///c:/laragon/www/YamOS/src/drivers/input/keyboard.c)
- Intercept `Alt+F1..F6` before routing to evdev
- Call `vtty_switch()` directly from IRQ handler (or queue a switch request)

---

### macOS-Style Desktop GUI Upgrade

#### [MODIFY] [compositor.c](file:///c:/laragon/www/YamOS/src/os/services/compositor/compositor.c)

**macOS Design Elements to Implement:**

1. **Top Menu Bar** (full-width, 24px, translucent)
   - Left: Apple-style YAM logo icon + current app name
   - Center: app menu items (File, Edit, View, Window, Help)
   - Right: Status icons (WiFi, Battery, Volume, Clock)

2. **Bottom Dock** (already exists, enhance it)
   - App icons with bounce animation on launch
   - Magnification on hover (scaled icon rendering)
   - Running indicator dot (white dot under active apps)
   - Spacer dividers between app groups

3. **Window Chrome (macOS Aqua-style)**
   - Traffic light buttons: Close (red), Minimize (yellow), Maximize (green)
   - Frosted glass titlebar (blur effect using box sampling)
   - Drop shadow rendered below windows (gradient falloff)
   - Window snap animations (spring physics on drag release)

4. **Spotlight Search** (Cmd-equivalent: `Super` key)
   - Centered search bar overlay
   - Searches running processes, app list, system info
   - Keyboard navigation

5. **Mission Control** (Ctrl+Up equivalent)
   - Shows all open windows tiled as thumbnails
   - Click to focus a window

6. **Desktop Widgets** (top-left corner)
   - System stats: CPU%, RAM, Network speed
   - Styled as frosted glass pill widgets

7. **Notification System**
   - Top-right slide-in notifications
   - Notification queue, auto-dismiss after 3s

---

### Kernel OS Structure Reorganization

#### New directory tree additions under `src/os/`:

```
src/os/bin/          [NEW] — init.c, login.c, sh.c
src/os/etc/          [NEW] — passwd.h, fstab.h (C header config tables)  
src/os/dev/          [NEW] — devfs.c, vtty.c (device node registry + VTs)
src/os/proc/         [NEW] — procfs.c (read-only /proc pseudo-filesystem)
```

#### [MODIFY] [Makefile](file:///c:/laragon/www/YamOS/Makefile)
- Add `src/os/bin/*.c` to kernel sources (init, login, sh are kernel tasks)
- Add `src/os/dev/*.c` to kernel sources (devfs, vtty)
- Add `src/os/proc/*.c` to kernel sources (procfs)
- Add new USB driver files: `src/drivers/usb/*.c`
- Add new FS driver files: `src/fs/fat32.c`, `src/fs/initrd.c`
- Add QEMU run target with NIC: `-netdev user,id=n0 -device e1000,netdev=n0`

---

## File Change Summary

| File | Action | Component |
|---|---|---|
| `src/cpu/smp.c` | Modify | C1 — Manual INIT-SIPI-SIPI |
| `src/cpu/smp_trampoline.asm` | **New** | C1 — Real-mode trampoline |
| `src/fs/fat32.c` + `.h` | **New** | C2 — FAT32 driver |
| `src/fs/initrd.c` + `.h` | **New** | C2 — In-memory initrd |
| `src/fs/vfs.c` | Rewrite | C2 — Real VFS layer |
| `src/os/dev/devfs.c` | **New** | C2 — /dev filesystem |
| `src/drivers/usb/xhci.c` + `.h` | **New** | C3 — XHCI controller |
| `src/drivers/usb/usb_core.c` + `.h` | **New** | C3 — USB core |
| `src/drivers/usb/hid.c` + `.h` | **New** | C3 — HID keyboard/mouse |
| `src/drivers/bus/api.c` | Modify | C3 — Wire XHCI |
| `src/net/arp.c` | Rewrite | C4 — ARP with cache |
| `src/net/ip.c` | Rewrite | C4 — IPv4 |
| `src/net/icmp.c` | Rewrite | C4 — ICMP ping |
| `src/net/udp.c` | Rewrite | C4 — UDP sockets |
| `src/net/tcp.c` | Rewrite | C4 — Full TCP |
| `src/net/dhcp.c` | Rewrite | C4 — DHCP client |
| `src/net/dns.c` | Rewrite | C4 — DNS resolver |
| `src/net/net_socket.h` | **New** | C4 — Socket API types |
| `src/drivers/net/e1000.c` | Modify | C4 — TX/RX rings |
| `src/os/lib/libc/stdio.c` | Modify | C5 — fopen/fread/fwrite |
| `src/os/lib/libc/stdlib.c` | Modify | C5 — calloc/realloc/qsort |
| `src/os/lib/libc/unistd.c` + `.h` | **New** | C5 — POSIX syscall wrappers |
| `src/os/lib/libc/errno.h` | **New** | C5 — Error codes |
| `src/os/lib/libc/ctype.h` | **New** | C5 — Character classification |
| `src/os/lib/libc/sys/socket.h` | **New** | C5 — Socket API |
| `src/fs/elf.c` | Modify | C6 — VFS exec + argv |
| `src/sched/sched.c` | Modify | C6 — fork/wait/exit |
| `src/os/bin/init.c` | **New** | C6 — PID 1 init |
| `src/os/dev/vtty.c` + `.h` | **New** | C7 — Virtual TTY engine |
| `src/os/services/compositor/compositor.c` | Modify | C7 + macOS Desktop |
| `src/drivers/input/keyboard.c` | Modify | C7 — Alt+Fx switching |
| `Makefile` | Modify | Build all new files |

**Total: ~25 new files, ~15 modified files**

---

## Verification Plan

### Automated Tests
- **SMP**: Serial output should show `[SMP] All N Application Processors booted`
- **FAT32**: Mount initrd, `vfs_open("/etc/passwd")` returns valid fd
- **TCP**: QEMU with `-netdev user` → kernel sends DHCP, gets IP, can ping 10.0.2.2
- **VTY**: Pressing Alt+F2 switches to a blank terminal VT

### QEMU Test Commands
```bash
# Network test (DHCP + ping)
qemu-system-x86_64 -cdrom build/yamkernel.iso -m 256M -smp 4 \
  -netdev user,id=n0 -device e1000,netdev=n0 -serial stdio

# VT test
qemu-system-x86_64 -cdrom build/yamkernel.iso -m 256M -smp 2 -serial stdio

# USB HID (with XHCI emulation)
qemu-system-x86_64 -cdrom build/yamkernel.iso -m 256M \
  -device nec-usb-xhci -device usb-kbd -device usb-mouse -serial stdio
```

### Manual Verification
- Desktop looks like macOS: top menu bar, bottom dock with magnification, traffic light buttons
- Alt+F1..F6 switches between 6 independent terminal sessions
- Terminal app can run a simple shell with `ls`, `cat /etc/passwd`
- `printf()` in user apps outputs to screen via `sys_write()`
