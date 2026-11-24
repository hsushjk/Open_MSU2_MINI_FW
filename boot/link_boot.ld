/* 常驻引导的链接脚本：只占 0x0000-0x0FFF（4 KB），永不擦除。
 * 应用从 0x1000 开始，所以引导里的这句 0xF000 上限 = 应用区最大到 0xF000。
 */
ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
}

_boot_stack_top = ORIGIN(RAM) + LENGTH(RAM);
_boot_gp = ORIGIN(RAM) + 0x800;

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
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > RAM AT> FLASH
    _boot_sidata = LOADADDR(.data);
    _boot_sdata = ADDR(.data);
    _boot_edata = ADDR(.data) + SIZEOF(.data);

    .bss : {
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
    } > RAM

    ASSERT(SIZEOF(.text) + SIZEOF(.rodata) <= 0x1000 - 0x200, "bootloader too big")
}