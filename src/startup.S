/* Startup for CH32X033 (RV32IMC).
 *
 * 复位后从 0x00000000 取第一条指令，向量表就在这里。向量模式由 mtvec=7
 * 指定：基址 4，异常 n 跳到 4+4n。USBFS 是 IRQ 45，落在 0xB8。
 */
    .section .vectors, "ax"
    .globl _vectors
_vectors:
    .option norvc
    .globl _start
_start:
    j       reset_entry
    .rept   45
    .word   trap_handler
    .endr
    .word   USBFS_IRQHandler
    .rept   18
    .word   trap_handler
    .endr

    .text
    .option rvc
reset_entry:
    la      sp, _stack_top
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

#ifndef STARTUP_NO_CSR
    /* 向量模式：mtvec=7 表示基址 4 */
    li      t0, 7
    csrw    0x305, t0
#endif

    /* copy .data from flash to RAM */
    la      t0, _sidata
    la      t1, _sdata
    la      t2, _edata
1:  bgeu    t1, t2, 2f
    lw      t3, 0(t0)
    sw      t3, 0(t1)
    addi    t0, t0, 4
    addi    t1, t1, 4
    j       1b

    /* zero .bss */
2:  la      t1, _sbss
    la      t2, _ebss
3:  bgeu    t1, t2, 4f
    sw      zero, 0(t1)
    addi    t1, t1, 4
    j       3b

    /* 常驻引导（0x0000）转发中断：把我们的处理函数地址填进 RAM 表 0x20004000
       （第 1..63 项；第 0 项是复位入口，用不到）。 */
4:  la      t1, _vectors
    li      t2, 0x20004000
    li      t3, 1
5:  slli    t4, t3, 2
    add     t4, t1, t4
    lw      t5, 0(t4)
    li      t4, 0x20004000
    slli    t6, t3, 2
    add     t4, t4, t6
    sw      t5, 0(t4)
    addi    t3, t3, 1
    li      t6, 64
    blt     t3, t6, 5b

    /* mstatus = 0x88：MIE=1，开总中断 */
4:
#ifndef STARTUP_NO_CSR
    li      t0, 0x88
    csrw    0x300, t0
#endif
    call    main

    j       trap_loop

trap_loop:
    j       trap_loop

    .size reset_entry, . - reset_entry