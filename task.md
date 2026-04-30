# YamOS v0.4.0 — Task Tracker

## Phase 1: Kernel Filesystem (C2)
- [/] `src/fs/fat32.h` — FAT32 types and interface
- [/] `src/fs/fat32.c` — FAT32 read/write driver
- [/] `src/fs/initrd.h` + `initrd.c` — in-memory initrd
- [/] `src/fs/vfs.c` — rewrite with real mount table + path resolution
- [/] `src/os/dev/devfs.c` — /dev filesystem

## Phase 2: USB Stack (C3)
- [/] `src/drivers/usb/xhci.h` + `xhci.c` — XHCI controller
- [/] `src/drivers/usb/usb_core.h` + `usb_core.c` — USB core/enumeration
- [/] `src/drivers/usb/hid.h` + `hid.c` — HID keyboard/mouse/touch
- [/] `src/drivers/bus/api.c` — wire XHCI into usb_init()

## Phase 3: TCP/IP Stack (C4)
- [/] `src/net/net.h` — updated with socket/interface types
- [/] `src/net/net_socket.h` — POSIX socket API types
- [/] `src/net/arp.c` — ARP with cache
- [/] `src/net/ip.c` — IPv4 build/parse/route
- [/] `src/net/icmp.c` — ping responder+sender
- [/] `src/net/udp.c` — UDP sockets
- [/] `src/net/tcp.c` — Full TCP state machine
- [/] `src/net/dhcp.c` — DHCP client
- [/] `src/net/dns.c` — DNS resolver
- [/] `src/drivers/net/e1000.c` — complete TX/RX rings

## Phase 4: POSIX libc (C5)
- [/] `src/os/lib/libc/stdio.c` — add fopen/fread/fwrite/fclose
- [/] `src/os/lib/libc/stdlib.c` — add calloc/realloc/qsort/atoi
- [/] `src/os/lib/libc/unistd.h` + `unistd.c` — POSIX syscall wrappers
- [/] `src/os/lib/libc/errno.h` — error codes
- [/] `src/os/lib/libc/ctype.h` — character classification
- [/] `src/os/lib/libc/sys/socket.h` — socket API for userspace

## Phase 5: OS Structure + VTY (C6, C7)
- [/] `src/os/bin/init.c` — PID 1 init process
- [/] `src/os/etc/passwd.h` — user table config
- [/] `src/os/dev/vtty.h` + `vtty.c` — 6 virtual consoles
- [/] `src/os/proc/procfs.c` — /proc pseudo-filesystem
- [/] `src/fs/elf.c` — add VFS-based exec + argv

## Phase 6: macOS Desktop
- [/] `compositor.c` — top menu bar, dock magnification, traffic lights,
                        Spotlight, Mission Control, frosted shadows, notifications

## Phase 7: Build System
- [/] `Makefile` — add USB/new-fs sources, QEMU NIC target
- [/] `src/kernel/main.c` — wire vtty, init, devfs into boot sequence
