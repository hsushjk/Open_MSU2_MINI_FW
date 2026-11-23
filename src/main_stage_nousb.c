/* 分色诊断 A：完全不碰 USB（-Variant stage_nousb）
 *
 * 红 → 黄 → 绿，每色 1.5 秒，然后停在绿。用来确认
 * 屏幕链路与延时都没问题，把范围压到 usb_init() 这一处。
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

    hold(0xF800);   /* 红 */
    hold(0xFFE0);   /* 黄 */
    hold(0x07E0);   /* 绿 */

    for (;;)
        ;
}