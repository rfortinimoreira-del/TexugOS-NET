OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(stage2_entry)

PHDRS
{
    boot PT_LOAD FLAGS(5);
    writable PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x10000;
    .boot16 : { KEEP(*(.boot16)) } :boot
    .text : { *(.text*) } :boot
    .rodata : { *(.rodata*) *(.userimage) } :boot
    .data : { *(.data*) } :writable
    . = 0x00100000;
    .bss :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    } :writable
    __kernel_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
