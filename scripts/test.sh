#!/bin/bash
# ============================================================================
# YamKernel — Build, Run & Debug Script (WSL / Linux)
#
# Usage:
#   ./scripts/test.sh              Build + run with graphical QEMU window
#   ./scripts/test.sh serial       Build + run with serial log (build/serial.log)
#   ./scripts/test.sh headless     Build + run headless (serial on terminal)
#   ./scripts/test.sh debug        Build + run with GDB server on :1234
# ============================================================================

set -e
cd "$(dirname "$0")/.."

MODE="${1:-run}"

echo -e "\e[1;36m[1/2] Building YamKernel...\e[0m"
make iso

if [ $? -ne 0 ]; then
    echo -e "\e[1;31m[!] Build Failed. Please fix the compiler errors above.\e[0m"
    exit 1
fi

echo -e "\e[1;32m[2/2] Build Successful!\e[0m"

case "$MODE" in
    serial)
        echo -e "\e[1;33m[SERIAL] Launching with serial log → build/serial.log\e[0m"
        echo -e "\e[1;33m         Open a second terminal:  tail -f build/serial.log\e[0m"
        make run-serial
        ;;
    headless)
        echo -e "\e[1;33m[HEADLESS] Serial output on this terminal. Ctrl+A X to quit.\e[0m"
        make run-serial-only
        ;;
    debug)
        echo -e "\e[1;33m[DEBUG] QEMU frozen, waiting for GDB on localhost:1234\e[0m"
        echo -e "\e[1;33m        In another terminal: gdb build/yamkernel.elf -ex 'target remote :1234'\e[0m"
        make debug
        ;;
    *)
        echo -e "\e[1;33m[RUN] Launching QEMU (close window to stop)\e[0m"
        make run
        ;;
esac
