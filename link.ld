/* Standalone firmware linker script.
 * Flash 62 KB at 0x00000000, SRAM 20 KB at 0x20000000.
 */
ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 60K
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

    /* 复位面包屑：.noinit 不加载、也不在 .bss 里，启动代码不会碰它。
     * 放在 RAM 最底部，离栈顶（_stack_top）最远，绝不会被栈踩到。 */
    .noinit (NOLOAD) : {
        . = ALIGN(4);
        _snoinit = .;
        *(.noinit .noinit.*)
        . = ALIGN(4);
        _enoinit = .;
    } > RAM

    /* .ramfunc 与 .data 放在同一个输出段里：启动代码拷 .data 时
     * 顺带把“擦写内部 flash”的那几个函数也拷进 RAM。 */
    .data : {
        _sdata = .;
        *(.data .data.*)
        *(.ramfunc .ramfunc.*)
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