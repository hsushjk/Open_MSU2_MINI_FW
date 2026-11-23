/* USB 寄存器回读（-Variant usbregs）
 *
 * usb_init() 之后把几个关键寄存器读回来画成位图，白=1 黑=0，左起 bit7→bit0。
 *   y=6   白    BASE_CTRL   期望 0x29（0010 1001）
 *   y=20  绿    UDEV_CTRL   期望 0x81
 *   y=34  青    INT_EN      期望 0x03
 *   y=48  黄    AFIO_CTLR 低字节 期望 0x88
 *   y=62  洋红  INT_FG
 * 读取值是 0x00 或 0xFF 说明 USB 外设没有时钟，写入根本没进去。
 */
#include "fw.h"

#define USB_BASE 0x40023400u
#define AFIO_CTLR (*(volatile uint32_t *)0x40010018u)

static void bits(int y, uint16_t colour, uint8_t v)
{
    int b;
    for (b = 0; b < 8; b++) {
        int bit = 7 - b;
        lcd_fill(4 + b * 10, y, 8, 8, (v & (1u << bit)) ? colour : 0x0000);
    }
}

int main(void)
{
    volatile uint8_t *usb = (volatile uint8_t *)USB_BASE;

    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;
    board_clock_init();
    lcd_gpio_init();
    lcd_init();
    lcd_fill(0, 0, 160, 80, 0x4208);

    usb_init();

    bits(6,  0xFFFF, usb[0x00]);            /* BASE_CTRL */
    bits(20, 0x07E0, usb[0x01]);            /* UDEV_CTRL */
    bits(34, 0x07FF, usb[0x02]);            /* INT_EN */
    bits(48, 0xFFE0, (uint8_t)(AFIO_CTLR & 0xFFu)); /* AFIO CTLR 低字节 */
    bits(62, 0xF81F, usb[0x06]);            /* INT_FG */

    for (;;)
        ;
}