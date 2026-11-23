/* 最小固件（-Variant min）
 *
 * 只做三件事：开时钟、初始化屏幕、整屏填红，然后死循环。
 * 不碰 USB、不碰中断、启动文件也不写 mtvec / mstatus。
 * 用来把"固件问题"和"刷写或硬件问题"分开：
 *   能出红屏 → 屏幕、排线、刷写都正常，问题在 USB 那一半；
 *   出不了红屏 → 问题不在 USB 代码，先查刷写与硬件。
 */
#include "fw.h"

int main(void)
{
    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;

    board_clock_init();
    lcd_gpio_init();
    lcd_init();
    lcd_fill(0, 0, 160, 80, 0xF800);   /* 红 */

    for (;;)
        ;
}