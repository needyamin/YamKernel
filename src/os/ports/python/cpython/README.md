# YamOS CPython Port

This is the YamOS port target for real Python from python.org.

Current source:

- Official CPython source: `vendor/cpython/Python-3.14.4`
- Source URL: `https://www.python.org/ftp/python/3.14.4/Python-3.14.4.tar.xz`
- Python.org source page identified Python 3.14.4 as the latest stable Python 3
  release on 2026-05-02.

Current status:

- The source is vendored, but it is not linked into `build/python.elf` yet.
- The visible `python`/`pip` commands are CPython-only. Until YamOS has the
  libc, VFS, tty, process, signal, time, HTTPS, and package-storage APIs
  CPython/pip expect, they report port status and do not run fake interpreters.
- See `PORT_REQUIREMENTS.md` for the exact runtime and pip requirements.

Minimum CPython OS work before it can execute inside YamOS:

1. Add/verify CPython-required libc/POSIX functions: `errno`, `stat`, `fstat`,
   `isatty`, `getcwd`, `chdir`, `access`, `dup`, `getenv`, `setenv`,
   `clock_gettime`, locale stubs, and file descriptor behavior.
2. Provide blocking stdin/stdout/stderr through the YamOS terminal or VTTY.
3. Provide a writable filesystem path for `Lib/`, `__pycache__`, and user code.
4. Decide static-only module set; dynamic extension modules need loader support
   YamOS does not have yet.
5. Cross-compile CPython as a static userspace ELF and keep final output at
   `build/python.elf`.

Do not present any fallback interpreter as python.org CPython. CPython is the
target the user asked for.
