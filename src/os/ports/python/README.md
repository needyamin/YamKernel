# YamOS Python Port Slot

This directory is reserved for the real Python port.

Current truth:

- Official CPython source from python.org has been imported at
  `vendor/cpython/Python-3.14.4`.
- The YamOS-specific CPython port notes live in `src/os/ports/python/cpython`.
- CPython from python.org is the only Python source/runtime target in the repo.
- The visible terminal reserves `python`, `python3`, `py`, `pip`, and `pip3`
  for CPython only. Until CPython is linked, those commands show port status
  and do not run a fake interpreter.

Current integration points:

- The ISO now includes `/boot/python.elf`.
- The kernel detects it as `g_python_module`.
- The Wayland launcher and File menu can start it.
- The placeholder app is `src/os/apps/python.c`.

When the port is completed, replace `src/os/apps/python.c` or change the
`$(PYTHON_ELF)` Makefile rule to compile/link CPython from
`vendor/cpython/Python-3.14.4` while keeping the final output path as
`build/python.elf`.

Minimum app contract:

```c
void _start(void);
```

Useful YamOS userspace APIs:

- `exit(code)`
- `sleep_ms(ms)`
- `wl_create_surface(title, x, y, w, h)`
- `wl_map_buffer(surface_id, addr)`
- `wl_commit(surface_id)`
- `wl_poll_event(surface_id, &event)`
- `print(text)` for serial output

CPython needs much more POSIX, filesystem, process, signal, time, dynamic
loader, and libc behavior than YamOS currently exposes. The source is present;
the remaining work is the OS compatibility layer plus static userspace link.

Do not add or expose another Python-like fallback under the `python`,
`python3`, `py`, `pip`, or `pip3` command names.
