/* 分阶段诊断固件（-Variant stage）
 *
 * 每一步把整屏刷成一个颜色，用来判断固件死在哪一步：
 *   红   → 屏幕初始化完成
 *   黄   → USB 时钟、引脚、复位脉冲完成
 *   绿   → 端点 DMA 与模式寄存器配置完成
 *   青   → BASE_CTRL / 中断使能位 / DEV_ADDR 写完
 *   白   → 打开 NVIC 中断后进入主循环
 *
 * 如果颜色反复从头开始，说明 CPU 在不断复位；如果停在某一色不动，
 * 停在哪个颜色就说明后面那一步出了异常。
 */
#include "fw.h"

static void screen(uint16_t colour)
{
    lcd_fill(0, 0, 160, 80, colour);
    delay_ms(700);
}

int main(void)
{
    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;

    lcd_gpio_init();
    lcd_init();
    screen(0xF800);            /* 红：屏幕可用 */

    usb_irq_enable = 0;        /* 先不开中断，隔离中断路径 */
    usb_init();
    screen(0xFFE0);            /* 黄：USB 寄存器写完 */

    screen(0x07E0);            /* 绿：中间检查点 */
    screen(0x07FF);            /* 青：即将打开中断 */

    NVIC_ISER1 = 0x2000u;      /* 打开 USBFS 中断 */
    screen(0xFFFF);            /* 白：中断开着且没复位 */

    for (;;)
        ;
}