/* 请求序列（-Variant usbforce）
 *
 * 第一排：bRequest 的颜色（9×12）
 *   黄 0x06 取描述符  绿 0x05 设地址  青 0x08 取配置  白 0x09 设配置
 *   洋红 0x00 取状态  灰 0x0A 取接口  红 其它
 * 第二排：描述符类型的颜色（同样顺序）
 *   黄 0x01 设备  橙 0x02 配置  洋红 0x03 字符串  青 0x22 HID 报文
 *   白 0x06 设备限定  灰 其它
 * 右上角白块 = 程序在跑；收到设配置整屏转绿。
 */
#include "fw.h"

static void req_colour(uint8_t req, uint16_t *c)
{
    switch (req) {
    case 0x00: *c = 0xF81F; break;
    case 0x05: *c = 0x07E0; break;
    case 0x06: *c = 0xFFE0; break;
    case 0x07: *c = 0xFD20; break;
    case 0x08: *c = 0x07FF; break;
    case 0x09: *c = 0xFFFF; break;
    case 0x0A: *c = 0x8410; break;
    default:   *c = 0xF800; break;
    }
}

static void type_colour(uint8_t t, uint16_t *c)
{
    switch (t) {
    case 0x01: *c = 0xFFE0; break;
    case 0x02: *c = 0xFD20; break;
    case 0x03: *c = 0xF81F; break;
    case 0x06: *c = 0xFFFF; break;
    case 0x22: *c = 0x07FF; break;
    default:   *c = 0x8410; break;
    }
}

int main(void)
{
    uint32_t seen = 0xFFFFFFFFu;
    uint32_t i;

    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;
    board_clock_init();
    lcd_gpio_init();
    lcd_init();
    lcd_fill(0, 0, 160, 80, 0x0000);
    lcd_fill(152, 0, 8, 8, 0xFFFF);

    usb_init();

    for (;;) {
        if (cfg_selected != 0u) {
            lcd_fill(0, 0, 160, 80, 0x07E0);
            for (;;)
                ;
        }
        if (usb_req_count != seen) {
            for (i = 0; i < 16u && i < usb_req_count; i++) {
                uint16_t c;
                req_colour(usb_req_log[i], &c);
                lcd_fill((int)i * 10, 16, 8, 12, c);
                type_colour(usb_req_type[i], &c);
                lcd_fill((int)i * 10, 34, 8, 12, c);
            }
            seen = usb_req_count;
        }
        delay_ms(20);
    }
}