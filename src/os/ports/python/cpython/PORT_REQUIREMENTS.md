# CPython Runtime Requirements For YamOS

The user wants the real 64-bit Python from python.org, usable from Terminal as
`python`, plus `pip install` for apps and data science code.

No fallback interpreter is allowed under `python`, `python3`, `py`, `pip`, or
`pip3`.

## Required Before `python` Can Execute

- Static CPython userspace ELF linked as `build/python.elf`.
- Blocking stdin/stdout/stderr connected to Terminal or VTTY.
- Enough libc/POSIX for CPython startup:
  `errno`, `stat`, `fstat`, `lstat`, `isatty`, `getcwd`, `chdir`, `access`,
  `dup`, `dup2`, `getenv`, `setenv`, `unsetenv`, `clock_gettime`, `time`,
  `localtime`, locale stubs, file descriptor flags, and directory iteration.
- Userland floating-point ABI support. User CFLAGS now enable SSE2/SSE math, and
  the scheduler has per-task FPU/XSAVE state. This still needs deeper testing
  with floating-point user programs before CPython can rely on it.
- Writable filesystem mount for:
  `/usr/lib/python3.14`, `/usr/bin`, `/tmp`, user scripts, and `__pycache__`.
- Install CPython `Lib/` into the ISO or writable system volume.

## Required Before `pip install` Can Work

- Working CPython with `ensurepip` or bundled pip wheel.
- TCP sockets exposed through libc socket wrappers.
- DNS, HTTPS/TLS certificate validation, and an HTTPS client stack.
- Persistent writable package directory such as
  `/usr/lib/python3.14/site-packages`.
- Build support for packages with native extensions:
  compiler toolchain, headers, shared/static extension strategy, and enough
  process support for subprocess-based builds.

## Data Science Reality

Pure-Python packages can work after CPython, pip, HTTPS, and writable storage
are done.

Packages like NumPy, pandas, SciPy, PyTorch, and scikit-learn need far more:
native extension loading or static builds, math/libm coverage, threads,
possibly BLAS/LAPACK, and much more memory than the current 256 MB QEMU run.
