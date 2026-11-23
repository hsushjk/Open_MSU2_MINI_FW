/* 分色诊断 B：调用 usb_init()，但不开中断（-Variant stage_usb）
 *
 * 红 → 黄（usb_init 之前）→ 绿（usb_init 返回后）
 * 若红黄出现、绿不出现，说明卡在 usb_init() 内部。
 */
#include "fw.h"

static void hold(uint16_t colour)
{
    lcd_fill(0, 0, 160, 80, colour);
    delay_ms(1500);
}

int main(void)
{
    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;

    board_clock_init();
    lcd_gpio_init();
    lcd_init();

    hold(0xF800);   /* 红：屏幕可用 */
    hold(0xFFE0);   /* 黄：即将调用 usb_init */

    usb_irq_enable = 0;
    usb_init();

    hold(0x07E0);   /* 绿：usb_init 正常返回 */

    for (;;)
        ;
}