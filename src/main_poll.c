/* 枚举进度（-Variant poll 配 -DPOLL_MODE）
 *
 *   SET_CONFIGURATION 到达  -> 整屏绿
 *   否则四排横条：
 *     y=10  绿  SETUP 次数，每 1 次一格
 *     y=22  红  总线复位次数，每 1 次一格
 *     y=34  青  IN 完成次数，每 1 次一格
 *     y=46  白  中断次数，每 8 次一格
 *   右上角白方块 = 程序在跑
 */
#include "fw.h"

static uint32_t last[4] = { 0, 0, 0, 0 };
static int painted_green = 0;

static void bar(int slot, int y, uint16_t colour, uint32_t value, uint32_t per)
{
    uint32_t cells = value / per;
    uint32_t i;
    if (cells > 20u) cells = 20u;
    if (cells <= last[slot]) return;
    for (i = last[slot]; i < cells; i++)
        lcd_fill((int)i * 8, y, 8, 6, colour);
    last[slot] = cells;
}

int main(void)
{
    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;
    board_clock_init();
    lcd_gpio_init();
    lcd_init();
    lcd_fill(0, 0, 160, 80, 0x0000);
    lcd_fill(150, 0, 8, 8, 0xFFFF);

    usb_init();

    for (;;) {
        usb_service();

        if (cfg_selected != 0u) {
            if (!painted_green) {
                lcd_fill(0, 0, 160, 80, 0x07E0);
                painted_green = 1;
            }
            continue;
        }

        bar(0, 10, 0x07E0, usb_setup_count, 1);
        bar(1, 22, 0xF800, usb_reset_count, 1);
        bar(2, 34, 0x07FF, usb_in_count,    1);
        bar(3, 46, 0xFFFF, usb_irq_count,   8);
    }
}