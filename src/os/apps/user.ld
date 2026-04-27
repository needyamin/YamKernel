/* YamKernel — User-Space ELF linker script
 * Places .text at 0x400000 (standard Linux x86_64 executable base).
 */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5);  /* PF_R | PF_X */
    data PT_LOAD FLAGS(6);  /* PF_R | PF_W */
}

SECTIONS {
    . = 0x400000;

    .text : {
        *(.text .text.*)
    } :text

    .rodata : {
        *(.rodata .rodata.*)
    } :text

    . = ALIGN(4096);

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
