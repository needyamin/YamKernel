; YamKernel — Drop from ring 0 to ring 3.
; void enter_user_mode(u64 rip, u64 rsp, u64 argc, u64 argv, u64 envp);
;   RDI = user RIP   RSI = user RSP   RDX = argc   RCX = argv   R8 = envp
;
; void enter_user_mode_ldso(u64 rip, u64 rsp);
;   Musl dynamic linker entry: _dl_start(void *sp). Linux passes initial stack
;   in RDI; user RIP is ld.so e_entry. Set RDI = user RSP before iretq.

bits 64
section .text
global enter_user_mode
global enter_user_mode_ldso

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
    mov   rdi, rdx                 ; user argc
    mov   rsi, rcx                 ; user argv
    mov   rdx, r8                  ; user envp
    xor   rcx, rcx
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

enter_user_mode_ldso:
    cli

    push  0x1B                  ; SS = user data | RPL3
    push  rsi                   ; user RSP (initial stack top / argc cell)
    push  0x202                 ; RFLAGS (IF=1)
    push  0x23                  ; CS = user code | RPL3
    push  rdi                   ; user RIP = ld.so entry

    swapgs
    mov   rdi, rsi              ; musl _dl_start(void *sp): sp -> RDI
    xor   rsi, rsi
    xor   rdx, rdx
    xor   rcx, rcx
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
