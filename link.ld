/* Standalone firmware linker script.
 * Flash 62 KB at 0x00000000, SRAM 20 KB at 0x20000000.
 */
ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 62K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
}

_stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .text : {
        KEEP(*(.vectors))
        *(.text .text.*)
        . = ALIGN(4);
    } > FLASH

    .rodata : {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(4);
    } > FLASH

    .data : {
        _sdata = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        _edata = .;
    } > RAM AT> FLASH
    _sidata = LOADADDR(.data);

    .bss : {
        _sbss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        _ebss = .;
    } > RAM

    __global_pointer$ = _sdata + 0x800;
}