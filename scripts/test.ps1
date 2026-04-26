# ============================================================================
# YamKernel — Build, Run & Debug Script (Windows PowerShell)
#
# Usage:
#   .\scripts\test.ps1              Build + run with graphical QEMU window
#   .\scripts\test.ps1 serial       Build + run with serial log
#   .\scripts\test.ps1 headless     Build + run headless (serial on terminal)
#   .\scripts\test.ps1 debug        Build + run with GDB server on :1234
# ============================================================================

param([string]$Mode = "run")

Set-Location -Path "$PSScriptRoot\.."

Write-Host "[1/2] Building YamKernel in WSL..." -ForegroundColor Cyan
wsl -d Ubuntu -- bash -c "cd /mnt/c/laragon/www/kernel && make iso"

if ($LASTEXITCODE -ne 0) {
    Write-Host "[!] Build Failed. Please fix the compiler errors above." -ForegroundColor Red
    exit 1
}

Write-Host "[2/2] Build Successful!" -ForegroundColor Green

switch ($Mode) {
    "serial" {
        Write-Host "[SERIAL] Launching with serial log -> build/serial.log" -ForegroundColor Yellow
        Write-Host "         View log: wsl -d Ubuntu -- tail -f /mnt/c/laragon/www/kernel/build/serial.log" -ForegroundColor Yellow
        wsl -d Ubuntu -- bash -c "cd /mnt/c/laragon/www/kernel && make run-serial"
    }
    "headless" {
        Write-Host "[HEADLESS] Serial output on this terminal. Ctrl+A X to quit." -ForegroundColor Yellow
        wsl -d Ubuntu -- bash -c "cd /mnt/c/laragon/www/kernel && make run-serial-only"
    }
    "debug" {
        Write-Host "[DEBUG] QEMU frozen, waiting for GDB on localhost:1234" -ForegroundColor Yellow
        Write-Host "        In another terminal: wsl gdb build/yamkernel.elf -ex 'target remote :1234'" -ForegroundColor Yellow
        wsl -d Ubuntu -- bash -c "cd /mnt/c/laragon/www/kernel && make debug"
    }
    default {
        Write-Host "[RUN] Launching QEMU (close window to stop)" -ForegroundColor Yellow
        wsl -d Ubuntu -- bash -c "cd /mnt/c/laragon/www/kernel && make run"
    }
}
