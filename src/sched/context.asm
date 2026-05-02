; YamKernel — kernel-thread context switch
; void context_switch(u64 *old_rsp, u64 new_rsp)
;   rdi = &old->rsp     rsi = new->rsp
; Saves callee-saved regs onto current stack, swaps RSP, restores new context.

bits 64
section .text
global context_switch

context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    pushfq

    mov  [rdi], rsp        ; save old RSP
    mov  rsp, rsi          ; load new RSP

    popfq
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret

; Trampoline for first run of a task. Initial stack (from low to high):
;   [rflags][r15..rbp][trampoline_RIP][arg][entry]
; context_switch's popfq+6 pops+ret lands here. Stack may not be 16-aligned
; for `call rax`, so we force-align it.
extern task_exit
global task_trampoline
task_trampoline:
    pop  rdi               ; arg
    pop  rax               ; entry
    and  rsp, -16          ; align stack for SysV ABI
    sti
    call rax
    call task_exit         ; If task returns, exit properly instead of hlt
.dead:
    cli
    hlt
    jmp  .dead

section .note.GNU-stack noalloc noexec nowrite progbits
