/* 统计回送（-Variant ping）：不初始化屏幕，周期把 USB 事件计数与最近一次 OUT 的内容发出去。
 * 每包 8 字节：[0xA5, irq, out, reset, len, buf0, buf1, 0x5A]
 * 收到任何字节都原样回送，便于看回环。 */
#include "fw.h"

static uint8_t rx[512];

int main(void)
{
    uint32_t tick = 0;

    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;
    board_clock_init();

    usb_init();

    for (;;) {
        int n = usb_read(rx, (int)sizeof(rx));

        if (n > 0)
            usb_write(rx, n);

        if (++tick >= 30000u) {
            uint8_t st[8];
            tick = 0u;
            st[0] = 0xA5u;
            st[1] = (uint8_t)(usb_irq_count & 0xFFu);
            st[2] = (uint8_t)(usb_out_count & 0xFFu);
            st[3] = (uint8_t)(usb_reset_count & 0xFFu);
            st[4] = (uint8_t)(usb_last_len & 0xFFu);
            st[5] = usb_last_b0;
            st[6] = usb_last_b1;
            st[7] = 0x5Au;
            usb_write(st, 8);
        }
    }
}