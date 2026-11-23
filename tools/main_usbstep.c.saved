/* USB 中断事件计数显示（-Variant usbstep）
 *
 * 冷启动：等电源稳定 → 硬复位脉冲 → 初始化两次 → 灰底
 * 然后完整跑一遍 usb_init()，成功则在右下角点一个洋红块。
 * 主循环把中断里统计到的四类事件画成横条：
 *   y=40 绿    SETUP 包（正常枚举会先有一串）
 *   y=46 洋红  IN  包
 *   y=52 青    OUT 包
 *   y=58 橙    其它（含"标志位无法识别"的情况）
 * 每 8 次事件点亮一格，每格 8×5 像素，最多 20 格。
 * 右下角的小方块每 100 ms 翻转一次，用来判断主循环是否还在跑。
 */
#include "fw.h"

#define RCC_REG_BASE 0x40021000u

static uint32_t g_step_canary = 0x5A5A5A5Au;

static uint16_t bar_cells(uint32_t n)
{
    uint32_t c = n / 8u;
    if (c > 20u) c = 20u;
    return (uint16_t)c;
}

static void draw_bar(int y, uint16_t colour, uint32_t n)
{
    static uint16_t shown[4];
    int idx = (y - 40) / 6;
    uint16_t cells = bar_cells(n);
    int i;

    if (idx < 0 || idx > 3) return;
    if (shown[idx] == cells) return;
    for (i = 0; i < 20; i++)
        lcd_fill(i * 8, y, 8, 5, (i < (int)cells) ? colour : 0x0000);
    shown[idx] = cells;
}

int main(void)
{
    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;

    board_clock_init();
    lcd_gpio_init();

    delay_ms(600);
    GPIOA->BCR = 1u;
    delay_ms(30);
    GPIOA->BSHR = 1u;
    delay_ms(150);
    lcd_init();
    delay_ms(50);
    lcd_init();

    if (g_step_canary != 0x5A5A5A5Au) {
        lcd_fill(0, 0, 160, 80, 0x001F);
        for (;;)
            ;
    }

    lcd_fill(0, 0, 160, 80, 0x4208);
    lcd_fill(0, 0, 4, 4, 0xFFFF);

    usb_init();
    lcd_fill(144, 72, 16, 8, 0xF81F);

    for (;;) {
        draw_bar(40, 0x07E0, usb_setup_count);
        draw_bar(46, 0xF81F, usb_in_count);
        draw_bar(52, 0x07FF, usb_out_count);
        draw_bar(58, 0xFC00, usb_other_count);
        lcd_fill(150, 76, 6, 4, (usb_irq_count & 1u) ? 0xFFFF : 0x0000);
        delay_ms(100);
    }
}