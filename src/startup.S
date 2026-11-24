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

/* 向量模式：mtvec=7 表示基址 4 */
    li      t0, 7
    csrw    0x305, t0

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

    /* mstatus = 0x88：MIE=1，开总中断 */
4:
    li      t0, 0x88
    csrw    0x300, t0
    call    main

    j       trap_loop

trap_loop:
    j       trap_loop

    .size reset_entry, . - reset_entry