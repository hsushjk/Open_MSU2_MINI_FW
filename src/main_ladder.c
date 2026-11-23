/* 阶梯诊断（-Variant ladder）
 *
 * 每 700 ms 换一次整屏颜色，最后停在带标记的终态：
 *   红  -> lcd_init 走通
 *   黄  -> lcd_fill 第一次成功
 *   绿  -> 连续调用正常
 *   深灰 + 左上白点 -> 进主循环
 *   中部洋红方块 -> 全部走通（终态）
 *
 * 判读：
 *   一直黑（只有背光）      = LCD 没初始化，问题在 lcd_gpio_init/lcd_init/gpio_mode
 *   停在红/黄/绿            = 停在第几步
 *   深灰+白点+洋红          = 显示链路完全正常
 */
#include "fw.h"

/* 故意让它落在 .data 段（有初值的全局变量）：用来验证"镜像里有 .data"本身
 * 会不会导致不启动。初值读错就把屏幕刷蓝并停住。 */
uint32_t g_data_canary = 0xA5A5A5A5u;

static void step(uint16_t colour)
{
    lcd_fill(0, 0, 160, 80, colour);
    delay_ms(700);
}

int main(void)
{
    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;

    board_clock_init();
    lcd_gpio_init();
    lcd_init();

    if (g_data_canary != 0xA5A5A5A5u) {
        lcd_fill(0, 0, 160, 80, 0x001F); /* 蓝：.data 拷贝有问题 */
        for (;;)
            ;
    }

    step(0xF800);                       /* 红 */
    step(0xFFE0);                       /* 黄 */
    step(0x07E0);                       /* 绿 */

    lcd_fill(0, 0, 160, 80, 0x4208);    /* 深灰 */
    lcd_fill(0, 0, 4, 4, 0xFFFF);       /* 左上白点 */
    delay_ms(700);

    lcd_fill(72, 34, 16, 12, 0xF81F);   /* 中部洋红：终态 */

    for (;;)
        ;
}