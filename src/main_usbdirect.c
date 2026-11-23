/* 收发双向验证（-Variant direct）
 *
 * 白底，四个标记：
 *   (0,0)  10×10 方块每 500 ms 翻转 —— 主循环在跑
 *   y=20   绿条 = 发送成功次数（每 2 次 2 px）
 *   y=30   红条 = 撞忙次数（每 20 次 2 px）
 *   y=40   蓝条 = 收到字节数（每字节 1 px）
 *
 * 每 400 ms 发一次 00 4D 53 4E 30 32；收到任何字节原样回送。
 */
#include "fw.h"

static uint8_t  blink;
static uint32_t beats;
static uint32_t ok_cnt;
static uint32_t busy_cnt;
static uint32_t rd_bytes;
static uint32_t shown_ok;
static uint32_t shown_busy;
static uint32_t shown_rd;

int main(void)
{
    static const uint8_t hello[6] = { 0x00, 'M', 'S', 'N', '0', '2' };
    uint8_t buf[64];
    int n;

    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;
    board_clock_init();
    lcd_gpio_init();
    lcd_init();
    lcd_fill(0, 0, 160, 80, 0xFFFF);

    usb_init();

    for (;;) {
        beats++;

        if ((beats & 0x1Fu) == 0u) {
            blink = (uint8_t)(blink ^ 1u);
            lcd_fill(0, 0, 10, 10, blink ? 0xF800 : 0xFFFF);
        }

        if ((beats % 40u) == 0u) {
            if (usb_write(hello, 6) > 0)
                ok_cnt++;
            else
                busy_cnt++;
        }

        if ((ok_cnt >> 1) != shown_ok) {
            uint32_t cells = ok_cnt >> 1;
            if (cells > 30u)
                cells = 30u;
            lcd_fill(0, 20, (int)cells * 2, 6, 0x07E0);
            shown_ok = ok_cnt >> 1;
        }
        if ((busy_cnt / 20u) != shown_busy) {
            uint32_t cells = busy_cnt / 20u;
            if (cells > 30u)
                cells = 30u;
            lcd_fill(0, 30, (int)cells * 2, 6, 0xF800);
            shown_busy = busy_cnt / 20u;
        }

        n = usb_read(buf, (int)sizeof(buf));
        if (n > 0) {
            rd_bytes += (uint32_t)n;
            if (rd_bytes != shown_rd) {
                uint32_t cells = rd_bytes > 60u ? 60u : rd_bytes;
                lcd_fill(0, 40, (int)cells, 6, 0x001F);
                shown_rd = rd_bytes;
            }
            if (n > 16)
                n = 16;
            usb_write(buf, n);          /* 回环 */
        }

        delay_ms(10);
    }
}