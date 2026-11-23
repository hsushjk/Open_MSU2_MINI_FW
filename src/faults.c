/* 异常/中断落脚点：把 mcause 直接画成整屏颜色
 *
 * 之前所有向量除了 0 号和 45 号都指向 `j trap_loop` 死循环，
 * 现场表现为"屏幕停在黑底，什么也看不见"——分不清是死循环还是没跑到。
 * 现在换成把 mcause 编成颜色，一眼就能读出异常类型。
 *
 *   底色 = mcause & 0xF 对应的颜色
 *   左上角白块 = mcause 最高位为 1（中断，不是异常）
 *   左上角第二块洋红 = mcause & 0x10
 */
#include "fw.h"

static const uint16_t cause_colour[16] = {
    0x0000, /* 0 指令地址未对齐     黑 */
    0xF800, /* 1 取指访问错误       红 */
    0x07E0, /* 2 非法指令           绿 */
    0x001F, /* 3 断点               蓝 */
    0xFFE0, /* 4 载入地址未对齐     黄 */
    0x07FF, /* 5 载入访问错误       青 */
    0xF81F, /* 6 存储地址未对齐     洋红 */
    0xFFFF, /* 7 存储访问错误       白 */
    0xFD20, /* 8 用户态 ecall       橙 */
    0xF810, /* 9 机器态 ecall       粉 */
    0x8010, /* 10 保留             紫 */
    0x8410, /* 11 保留             灰 */
    0x8200, /* 12 保留             橄榄 */
    0x4208, /* 13 保留             深灰 */
    0x0010, /* 14 保留             深蓝 */
    0x4000, /* 15 保留             深红 */
};

static inline uint32_t rd_mcause(void)
{
    uint32_t v;
    __asm__ volatile ("csrr %0, mcause" : "=r"(v));
    return v;
}

void __attribute__((interrupt("machine"))) trap_handler(void)
{
    uint32_t cause = rd_mcause();

    lcd_fill(0, 0, 160, 80, cause_colour[cause & 0xFu]);

    if (cause & 0x80000000u)
        lcd_fill(2, 2, 12, 12, 0xFFFF);   /* 这是中断，不是异常 */
    if (cause & 0x10u)
        lcd_fill(18, 2, 12, 12, 0xF81F);  /* 码值的第 4 位 */

    for (;;)
        ;
}