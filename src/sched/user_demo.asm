; YamKernel — Tiny ring-3 demo program.
; Position-independent (RIP-relative) so it can run from any user virt address.
; Issues SYS_WRITE then SYS_SLEEPMS in a loop.

bits 64
section .text
global user_demo_start, user_demo_end

user_demo_start:
.loop:
    mov   rax, 1                ; SYS_WRITE
    mov   rdi, 1                ; fd
    lea   rsi, [rel msg]
    mov   rdx, msg_len
    syscall

    mov   rax, 5                ; SYS_SLEEPMS
    mov   rdi, 250              ; 250 ms
    syscall

    jmp   .loop

msg:    db "[USER] hi from ring3", 10
msg_len equ $-msg
user_demo_end:
