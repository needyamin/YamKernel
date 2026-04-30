# YamOS Python Port Slot

This directory is reserved for a real Python or MicroPython port.

Current integration points:

- The ISO now includes `/boot/python.elf`.
- The kernel detects it as `g_python_module`.
- The Wayland launcher and File menu can start it.
- The placeholder app is `src/os/apps/python.c`.

When you bring real Python source later, replace `src/os/apps/python.c` or
change the `$(PYTHON_ELF)` Makefile rule to compile your port sources while
keeping the final output path as `build/python.elf`.

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

Recommended first real port target: MicroPython, because CPython needs much
more POSIX, filesystem, dynamic loader, signals, and libc behavior.
