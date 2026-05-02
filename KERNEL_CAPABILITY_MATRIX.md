# YamOS Kernel Capability Matrix

This file is the standing comparison against Linux-class OS foundations. Update
it whenever kernel, driver, memory, storage, process, or networking work changes.

Legend:
- DONE: implemented and boot-tested
- PARTIAL: exists but is incomplete or missing production hardening
- MISSING: not implemented yet

## Required For Real x86_64 OS Compatibility

These are standing requirements before YamOS can claim it can install and run
general browser engines, programming languages, package tools, and ordinary
x86_64 operating-system workloads.

| Requirement | YamOS State | Notes |
| --- | --- | --- |
| Persistent disk/storage driver fully mounted as writable system volume | PARTIAL | virtio-blk disk registers with block core and FAT32 volumes auto-mount under /mnt; root is still initrd/ramfs |
| Full POSIX/Linux syscall compatibility | MISSING | syscall audit, errno model, path APIs, process APIs, socket APIs, permissions |
| `execve` and stronger process model | MISSING | replace current stubs with executable replacement, argv/envp, wait/exit hardening |
| Dynamic linker / shared libraries | MISSING | ELF interpreter, relocation loader, shared object search path, libc ABI |
| Threads and signals | MISSING | pthread/TLS/signal delivery/signal masks required by runtimes and browsers |
| Real permissions/users/security | MISSING | persistent users, UID/GID, file modes, capabilities, sandboxing |
| TLS/HTTPS + certificate store | PARTIAL | certificate-store bootstrap and outbound TLS ClientHello/ServerHello probe exist; full record encryption, certificate chain validation, and HTTPS downloader remain |
| Nonblocking sockets, `select`/`epoll` | MISSING | required by browsers, servers, package managers, and async runtimes |
| Package manager/downloader | MISSING | depends on signatures, manifests, install database, downloader transactions |
| Full browser engine support | MISSING | HTML5 parser, DOM, CSS, JS, images, fonts, storage, sandboxing |
| Language runtime porting layer | MISSING | POSIX/libc coverage, filesystem, threads, signals, sockets, dynamic linking |
| Real hardware driver coverage beyond current QEMU-focused path | MISSING | QEMU virtio-blk exists; still needs AHCI/NVMe, GPU, USB classes, Wi-Fi, audio, power, broader NICs |

## Missing Compatibility Checklist

These are the concrete compatibility surfaces still missing or incomplete for
Linux/POSIX-style ports.

| Compatibility Area | Missing / Incomplete Items |
| --- | --- |
| Process execution | `execve`, `execvpe`, argv/envp setup, interpreter scripts, process replacement, robust `waitpid`, exit status semantics |
| Dynamic linking | ELF `PT_INTERP`, shared library loader, relocations, PLT/GOT, `dlopen`, `dlsym`, `dlclose`, library search paths |
| Threads | kernel threads for user processes, pthread ABI, TLS, thread-local errno, robust futex behavior, thread join/detach |
| Signals | signal delivery, masks, handlers, default actions, `sigaction`, `sigprocmask`, timers and interruption semantics |
| File API | PARTIAL: disk-backed FAT32 create/write/truncate/unlink works for regular files; missing `rename`, `link`, `symlink`, `readlink`, `stat/lstat/fstat` accuracy, `fcntl`, file locks, permissions |
| Directories/path APIs | PARTIAL: per-process cwd, `chdir`, `getcwd`, and relative VFS paths exist; missing `openat` family and mount namespaces |
| Memory API | file-backed `mmap`, shared mappings, `msync`, `mremap`, guard/protection auditing, overcommit policy |
| Sockets/network | nonblocking mode, `select`, `poll` hardening, `epoll`, `getsockopt/setsockopt`, `getsockname/getpeername`, UDP sockets, IPv6 |
| Time APIs | monotonic/realtime clocks, `nanosleep`, timers, timezone/localtime correctness |
| Security | persistent users, UID/GID, file mode enforcement, capabilities, sandbox/process isolation, secure random |
| Terminal/PTY | PTY master/slave, termios, line discipline, job control, shell process groups |
| Package/runtime support | HTTPS downloader, certificate validation, signatures, package database, install/remove/upgrade transactions |
| Browser engine support | TLS, DOM, CSS layout, JS runtime hosting, image/font codecs, cache/cookies/storage, sandboxing |
| Hardware compatibility | virtio-blk/AHCI/NVMe, GPU modesetting/acceleration, USB storage/HID classes, audio, Wi-Fi, power management |

## Boot And Platform

| Area | YamOS State | Next Work |
| --- | --- | --- |
| x86_64 long mode boot | DONE | UEFI/bare-metal test matrix |
| Limine framebuffer/modules/memmap | DONE | richer boot diagnostics |
| ACPI table discovery | PARTIAL | full table validation, reboot/power paths |
| LAPIC/IOAPIC | PARTIAL | MSI/MSI-X interrupt programming, IRQ affinity, per-driver IRQ registration |
| SMP startup | PARTIAL | scheduler affinity, CPU hotplug model |
| HPET/PIT/RTC timers | PARTIAL | timer wheel, monotonic clock syscalls |

## Memory

| Area | YamOS State | Next Work |
| --- | --- | --- |
| Physical page allocator | PARTIAL | stress tests, larger-than-4GB descriptor support |
| Virtual memory/page tables | PARTIAL | robust MMIO mapper, kernel ASLR, guard coverage audit |
| Heap allocator | PARTIAL | fragmentation metrics, slab integration |
| mmap/brk/mprotect | PARTIAL | file-backed mmap, shared mappings |
| Copy-on-write foundation | PARTIAL | fork correctness tests |
| OOM handling | PARTIAL | reclaim, kill policy, per-cgroup limits |
| Swap/page cache | MISSING | block-backed page cache first |

## Drivers And Buses

| Area | YamOS State | Next Work |
| --- | --- | --- |
| PCI enumeration | DONE | bridge-aware recursive bus scan |
| PCI BAR decode | DONE | safe BAR sizing with decode masking, MSI/MSI-X capability parsing |
| PCI command helpers | DONE | generic driver probe/start hooks |
| Driver manager inventory | PARTIAL | lifecycle: probe, bind, start, stop, suspend, resume |
| e1000 network driver | PARTIAL | interrupt-driven RX/TX, link state events |
| USB/xHCI | PARTIAL | real device enumeration and class drivers |
| PS/2 keyboard/mouse | PARTIAL | layout tables, hotplug error handling |
| GPU/display | PARTIAL | modesetting, accelerated drivers |

## Storage And Filesystems

| Area | YamOS State | Next Work |
| --- | --- | --- |
| Block device core | DONE | real disk drivers must register here |
| virtio-blk | DONE | legacy/transitional PCI virtio-blk registers block devices and supports synchronous read/write requests |
| AHCI SATA | MISSING | PCI probe, HBA init, command slots |
| NVMe | MISSING | PCI probe, admin queue, IO queues |
| Partition parser | PARTIAL | MBR FAT32 detection, GPT basic-data FAT32-compatible detection, and superfloppy FAT32 fallback |
| Persistent root/system volume | PARTIAL | block-backed FAT32 mounts at /mnt/vd0; /home and /var are promoted to disk when available; root remains initrd/ramfs |
| FAT32 | PARTIAL | connect to block layer, fsck/recovery |
| VFS | PARTIAL | permissions, rename/unlink, directory iteration |
| Page cache | MISSING | required before serious file-backed mmap and speed |

## Processes, Security, And Syscalls

| Area | YamOS State | Next Work |
| --- | --- | --- |
| Scheduler | PARTIAL | priorities, affinity, latency tests |
| Userspace ELF loading | PARTIAL | dynamic linker story, signals, wait/exec hardening |
| Syscall table | PARTIAL | POSIX coverage audit |
| Users/accounts UI | PARTIAL | persistent accounts, password hashing |
| Permissions/security model | MISSING | UID/GID, file permissions, capabilities |
| Signals | MISSING | needed by many Unix programs |
| Threads/futex | MISSING | required for serious language runtimes and servers |

## Networking

| Area | YamOS State | Next Work |
| --- | --- | --- |
| Ethernet/e1000 | PARTIAL | interrupt-driven path |
| ARP/IPv4/UDP/TCP | PARTIAL | retransmit, congestion, UDP socket ABI |
| DHCP/DNS | PARTIAL | renew, multiple interfaces |
| HTTP client | PARTIAL | streaming, redirects, error model |
| TLS/HTTPS | PARTIAL | certificate-store bootstrap and TLS ClientHello/ServerHello probe exist; full handshake, crypto, validation, and HTTPS client remain |
| TCP socket syscalls | PARTIAL | AF_INET/SOCK_STREAM fd-backed socket/connect/listen/accept/read/write/close exists; needs nonblocking, peer addresses, error codes, select/epoll, stress tests |
| UDP socket syscalls | MISSING | sendto/recvfrom fd-backed datagram support |
| Routing/firewall | MISSING | routing table, packet filters |

## Rule For Future Work

When a user asks for a major feature, check this matrix first. If a required
kernel service is missing, implement the service or explicitly update this file
with the blocker and the next engineering step. Do not add fake application-level
workarounds that pretend missing kernel infrastructure exists.
