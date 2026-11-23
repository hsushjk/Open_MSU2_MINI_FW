/* 与上位机握手测试（-Variant host）
 *
 * 设备侧：枚举完成后主动发 6 字节 00 4D 53 4E 30 32（"MSN02"）。
 * 收到主机回的 00 4D 53 4E 43 4E（"MSNCN"）后整屏转绿，并把收到的帧数
 * 画成底部横条，用来确认数据通路。
 */
#include "fw.h"

static uint8_t  frame[6];
static int      frame_len;
static uint8_t  host_ready;
static uint32_t frames;
static uint32_t bytes;
static uint32_t last_frames;

static void draw_count(int y, uint16_t colour, uint32_t n, uint32_t per, uint32_t *last)
{
    uint32_t cells = n / per;
    uint32_t i;
    if (cells > 20u)
        cells = 20u;
    if (cells > *last) {
        for (i = *last; i < cells; i++)
            lcd_fill((int)i * 8, y, 8, 5, colour);
        *last = cells;
    }
}

int main(void)
{
    static const uint8_t hello[6] = { 0x00, 'M', 'S', 'N', '0', '2' };
    uint8_t buf[32];
    int n;

    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;
    board_clock_init();
    lcd_gpio_init();
    lcd_init();
    lcd_fill(0, 0, 160, 80, 0x0000);
    lcd_fill(152, 0, 8, 8, 0xFFFF);        /* 静止标记：程序在跑 */

    usb_init();

    for (;;) {
        if (cfg_selected != 0u && !host_ready) {
            usb_write(hello, 6);
            delay_ms(400);
        }

        n = usb_read(buf, (int)sizeof(buf));
        if (n > 0) {
            int i;
            for (i = 0; i < n; i++) {
                bytes++;
                frame[frame_len++] = buf[i];
                if (frame_len == 6) {
                    frames++;
                    if (frame[0] == 0x00u && frame[1] == 'M' && frame[2] == 'S' &&
                        frame[3] == 'N' && frame[4] == 'C' && frame[5] == 'N') {
                        host_ready = 1u;
                        lcd_fill(0, 0, 160, 80, 0x07E0);
                        lcd_fill(152, 0, 8, 8, 0xFFFF);
                    }
                    frame_len = 0;
                }
            }
            draw_count(70, 0xFFE0, frames, 1u, &last_frames);
        }
    }
}