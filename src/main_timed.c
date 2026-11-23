/* 时钟实测（临时占用 usbstep 变体）
 *
 * 白屏起步，然后每 2 秒切换一次红/白，共 10 次，最后停在 8×8 棋盘格。
 * 主频若为 48 MHz，从上电到棋盘格约 21 秒。
 * 实测时间明显更长，说明主频偏低（USB 全速设备要求正好 48 MHz）。
 */
#include "fw.h"

int main(void)
{
    int i, x, y;

    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;
    board_clock_init();
    lcd_gpio_init();
    lcd_init();

    lcd_fill(0, 0, 160, 80, 0xFFFF);

    for (i = 0; i < 10; i++) {
        delay_ms(2000);
        lcd_fill(0, 0, 160, 80, (i & 1) ? 0xFFFF : 0xF800);
    }

    for (y = 0; y < 80; y += 8)
        for (x = 0; x < 160; x += 8)
            lcd_fill(x, y, 8, 8, (((x / 8) + (y / 8)) & 1) ? 0xFFFF : 0x0000);

    for (;;)
        ;
}