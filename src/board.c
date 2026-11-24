/* 板级初始化：时钟与 flash 等待周期
 *
 *   1. 打开内部高速振荡器（HSI，48 MHz）
 *   2. flash 取指等待周期设为 2
 *   3. CFGR0 清掉 PLL 相关位，系统时钟留在 HSI
 *
 * 第 2 条是硬性要求：48 MHz 下 flash 等待周期为 0 时取指会不稳定，
 * 表现为偶发的花屏、错字或跑飞。
 */
#include "regs.h"

#define FLASH_ACTLR (*(volatile uint32_t *)0x40022000u)

void board_clock_init(void)
{
    RCC->CTLR |= 1u;                                  /* HSION */
    FLASH_ACTLR = (FLASH_ACTLR & ~3u) | 2u;           /* 2 wait states */
    RCC->CFGR0 &= 0xF8FFFF0Fu;                        /* PLL 关闭，SW 保持 HSI */
}