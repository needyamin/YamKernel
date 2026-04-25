/* ============================================================================
 * YamKernel Linker Script
 * Places the kernel in the higher-half at 0xFFFFFFFF80000000
 * Compatible with Limine bootloader protocol
 * ============================================================================ */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(kernel_entry)

/* Higher-half virtual base address */
KERNEL_VIRT_BASE = 0xFFFFFFFF80000000;
KERNEL_PHYS_BASE = 0x100000;

PHDRS
{
    text    PT_LOAD FLAGS((1 << 0) | (1 << 2));          /* Execute + Read */
    rodata  PT_LOAD FLAGS((1 << 2));                     /* Read only */
    data    PT_LOAD FLAGS((1 << 1) | (1 << 2));          /* Write + Read */
    dynamic PT_DYNAMIC FLAGS((1 << 1) | (1 << 2));       /* Write + Read */
}

SECTIONS
{
    . = KERNEL_VIRT_BASE;

    /* Executable code */
    .text : AT(ADDR(.text) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        _text_start = .;
        *(.text .text.*)
        _text_end = .;
    } :text

    . = ALIGN(4096);

    /* Read-only data */
    .rodata : AT(ADDR(.rodata) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        _rodata_start = .;
        *(.rodata .rodata.*)
        _rodata_end = .;
    } :rodata

    . = ALIGN(4096);

    /* Read-write initialized data */
    .data : AT(ADDR(.data) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        _data_start = .;
        *(.data .data.*)

        /* Limine requests are placed here */
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))

        _data_end = .;
    } :data

    /* Dynamic linking info (for Limine) */
    .dynamic : AT(ADDR(.dynamic) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        *(.dynamic)
    } :data :dynamic

    . = ALIGN(4096);

    /* Uninitialized data */
    .bss : AT(ADDR(.bss) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    } :data

    . = ALIGN(4096);
    _kernel_end = .;

    /* Discard unwanted sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
