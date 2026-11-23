/* USB 分段定位固件（-Variant usbstage）
 *
 * 目的：一次刷写就能分辨故障停在哪一段。启动代码里刻意放了一个带初值的
 * 全局变量，强制生成 .data 段，用来验证 .data 拷贝与 gp 是否正常。
 *
 * 判读：
 *   全黑（只有背光）              = 镜像根本没跑起来（启动代码/装载问题）
 *   纯蓝                          = .data 没被正确拷贝，或 gp 没初始化
 *   红 -> 黄 -> 绿 -> 深灰+白点    = LCD 与主流程正常
 *       停在这里没有洋红          = usb_init() 卡死
 *       出现右下角洋红块          = usb_init() 正常返回
 */
#include "fw.h"

uint32_t g_data_canary = 0xA5A5A5A5u;

static void fill(uint16_t colour)
{
    lcd_fill(0, 0, 160, 80, colour);
}

int main(void)
{
    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;

    board_clock_init();
    lcd_gpio_init();
    lcd_init();

    if (g_data_canary != 0xA5A5A5A5u) {
        fill(0x001F); /* 蓝 */
        for (;;)
            ;
    }

    fill(0xF800); /* 红 */
    delay_ms(700);
    fill(0xFFE0); /* 黄 */
    delay_ms(700);
    fill(0x07E0); /* 绿 */
    delay_ms(700);

    fill(0x4208);                  /* 深灰 */
    lcd_fill(0, 0, 4, 4, 0xFFFF);  /* 左上白点 */
    delay_ms(700);

    usb_init();

    lcd_fill(140, 72, 16, 8, 0xF81F); /* 右下角洋红：usb_init 已返回 */

    for (;;)
        ;
}