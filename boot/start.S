/* 常驻引导的启动代码：向量表在 0x0000，每个入口都转发到 RAM 里的地址表
 * （0x20004000 起，4 字节一项），应用启动时会把自己的处理函数填进去。
 * 这样应用搬到 0x1000 之后，中断依然能落到正确的处理函数上。
 */
    .section .vectors, "ax"
    .option norvc
    .globl _start
_start:
    j       boot_entry

    .macro  fwd n
    li      t0, 0x20004000
    lw      t0, (4 * \n)(t0)
    jr      t0
    .endm

    .rept   45
    fwd     1
    .endr
    fwd     46                  /* USBFS_IRQHandler：CH32 的 IRQ45 落在 4*46 */
    .rept   17
    fwd     47
    .endr

    .text
    .option rvc
boot_entry:
    la      sp, _boot_stack_top
    la      gp, _boot_gp
    call    boot_main
1:  j       1b