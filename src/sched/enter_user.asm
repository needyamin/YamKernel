; YamKernel — Drop from ring 0 to ring 3.
; void enter_user_mode(u64 rip, u64 rsp);
;   RDI = user RIP   RSI = user RSP

bits 64
section .text
global enter_user_mode

enter_user_mode:
    cli

    push  0x1B                  ; SS = user data | RPL3
    push  rsi                   ; user RSP
    push  0x202                 ; RFLAGS (IF=1, reserved bit 1)
    push  0x23                  ; CS = user code | RPL3
    push  rdi                   ; user RIP

    swapgs                      ; GS_BASE -> user value (0)
    xor   rax, rax
    xor   rbx, rbx
    xor   rcx, rcx
    xor   rdx, rdx
    xor   rbp, rbp
    xor   r8,  r8
    xor   r9,  r9
    xor   r10, r10
    xor   r11, r11
    xor   r12, r12
    xor   r13, r13
    xor   r14, r14
    xor   r15, r15
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
