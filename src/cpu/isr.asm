; ============================================================================
; YamKernel — Interrupt Service Routine Stubs (x86_64)
; Assembly stubs that save CPU state and call C dispatcher
; ============================================================================

[bits 64]
[extern isr_dispatch]
[extern kernel_main]

; ============================================================================
; Kernel Entry Point — called by bootloader
; ============================================================================
section .text
[global kernel_entry]
kernel_entry:
    ; Clear direction flag
    cld
    ; Call C kernel main
    call kernel_main
    ; If kernel_main returns, halt forever
.halt:
    cli
    hlt
    jmp .halt


; ============================================================================
; Macro: ISR stub WITHOUT error code (we push a dummy 0)
; ============================================================================
%macro ISR_NOERR 1
[global isr_stub_%1]
isr_stub_%1:
    push 0              ; dummy error code
    push %1             ; interrupt number
    jmp isr_common
%endmacro

; ============================================================================
; Macro: ISR stub WITH error code (CPU already pushed it)
; ============================================================================
%macro ISR_ERR 1
[global isr_stub_%1]
isr_stub_%1:
    push %1             ; interrupt number
    jmp isr_common
%endmacro

; ============================================================================
; Exception ISRs (0-31)
; ============================================================================
ISR_NOERR 0    ; Division By Zero
ISR_NOERR 1    ; Debug
ISR_NOERR 2    ; NMI
ISR_NOERR 3    ; Breakpoint
ISR_NOERR 4    ; Overflow
ISR_NOERR 5    ; Bound Range
ISR_NOERR 6    ; Invalid Opcode
ISR_NOERR 7    ; Device Not Available
ISR_ERR   8    ; Double Fault
ISR_NOERR 9    ; Coprocessor Segment Overrun
ISR_ERR   10   ; Invalid TSS
ISR_ERR   11   ; Segment Not Present
ISR_ERR   12   ; Stack Segment Fault
ISR_ERR   13   ; General Protection Fault
ISR_ERR   14   ; Page Fault
ISR_NOERR 15   ; Reserved
ISR_NOERR 16   ; x87 FP Exception
ISR_ERR   17   ; Alignment Check
ISR_NOERR 18   ; Machine Check
ISR_NOERR 19   ; SIMD FP Exception
ISR_NOERR 20   ; Virtualization Exception
ISR_ERR   21   ; Control Protection
ISR_NOERR 22   ; Reserved
ISR_NOERR 23   ; Reserved
ISR_NOERR 24   ; Reserved
ISR_NOERR 25   ; Reserved
ISR_NOERR 26   ; Reserved
ISR_NOERR 27   ; Reserved
ISR_NOERR 28   ; Reserved
ISR_NOERR 29   ; Reserved
ISR_NOERR 30   ; Reserved
ISR_NOERR 31   ; Reserved

; ============================================================================
; Hardware IRQ ISRs (32-47)
; ============================================================================
ISR_NOERR 32   ; PIT Timer (IRQ 0)
ISR_NOERR 33   ; Keyboard (IRQ 1)
ISR_NOERR 34   ; Cascade (IRQ 2)
ISR_NOERR 35   ; COM2 (IRQ 3)
ISR_NOERR 36   ; COM1 (IRQ 4)
ISR_NOERR 37   ; LPT2 (IRQ 5)
ISR_NOERR 38   ; Floppy (IRQ 6)
ISR_NOERR 39   ; LPT1 (IRQ 7)
ISR_NOERR 40   ; RTC (IRQ 8)
ISR_NOERR 41   ; Free (IRQ 9)
ISR_NOERR 42   ; Free (IRQ 10)
ISR_NOERR 43   ; Free (IRQ 11)
ISR_NOERR 44   ; PS/2 Mouse (IRQ 12)
ISR_NOERR 45   ; FPU (IRQ 13)
ISR_NOERR 46   ; Primary ATA (IRQ 14)
ISR_NOERR 47   ; Secondary ATA (IRQ 15)

; ============================================================================
; Common ISR handler — saves registers, calls C, restores registers
; ============================================================================
isr_common:
    ; Save all general-purpose registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Pass pointer to interrupt_frame_t as argument (RSP = frame pointer)
    mov rdi, rsp
    cld
    call isr_dispatch

    ; Restore all general-purpose registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Remove error code and interrupt number from stack
    add rsp, 16

    iretq

; ============================================================================
; GDT flush — load GDT and reload segment registers
; ============================================================================
[global gdt_flush]
gdt_flush:
    ; RDI = pointer to GDT pointer structure
    lgdt [rdi]

    ; Reload CS via far return
    push 0x08           ; Kernel code segment
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    ; Reload data segments
    mov ax, 0x10        ; Kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

; Mark stack as non-executable
section .note.GNU-stack noalloc noexec nowrite progbits
