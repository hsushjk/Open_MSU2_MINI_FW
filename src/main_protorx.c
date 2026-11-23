/* 协议固件（-Variant proto）
 *
 * 6 字节帧：[class, sub, b2, b3, b4, b5]
 *   class 0  sub 0x4D         握手（设备发 MSN02，主机回 MSNCN）
 *   class 2  sub 0            x0, y0
 *   class 2  sub 1            x1, y1（终点，不是宽高）
 *   class 2  sub 2            底色 / 页号
 *   class 2  sub 3  op 0      纯色填充   colour = b3:b4
 *                   op 7      开始流式   set_window(x0,y0,x1-1,y1-1) + CS 拉低
 *                   op 8      推一段     len = b3:b4（满段 0x0100）
 *                   op 9      结束
 *                   op 0x0A   旋转（暂不处理）
 *   class 2  sub 4            段缓冲填充：4 字节循环铺满 256 字节
 *   class 4                   段缓冲写入：seg[b1*4 ..] = b2..b5
 *   class 8                   读 ADC：回 [0x08, ch, 0, 0, hi, lo]
 */
#include "fw.h"

static uint8_t seg[256];
static uint8_t rx[1024];
static uint8_t debug_on;
static uint32_t raw_left;
static uint32_t raw_bytes_seen;                 /* 还剩多少原始像素字节 */
static uint8_t stage[512];
static uint32_t stage_len;
static uint32_t t_frame_start;
static volatile uint16_t last_frame_ms;

static uint32_t systick_now(void)
{
    return *(volatile uint32_t *)0xE000F008u;
}

static void systick_init(void)
{
    *(volatile uint32_t *)0xE000F010u = 0x00FFFFFFu;   /* CMP */
    *(volatile uint32_t *)0xE000F000u = 0x00000009u;   /* STRE | STE */
}
static volatile uint32_t frame_done;

static int x0, y0, x1, y1;
static int connected;
static uint8_t page_no;

static const uint8_t hello[6] = { 0x00, 0x4D, 0x53, 0x4E, 0x30, 0x32 };

static void seg_fill4(const uint8_t *p)
{
    int i;
    for (i = 0; i < 256; i += 4) {
        seg[i]     = p[0];
        seg[i + 1] = p[1];
        seg[i + 2] = p[2];
        seg[i + 3] = p[3];
    }
}

static void handle_frame(const uint8_t *f)
{
    uint8_t cls = f[0];
    uint8_t sub = f[1];
    uint16_t a = (uint16_t)((f[2] << 8) | f[3]);
    uint16_t b = (uint16_t)((f[4] << 8) | f[5]);

    if (cls == 0x00u) {
        if (sub == 0x5Au) {
            debug_on = f[2];
            return;
        }
        if (sub == 0x4Du) {
            connected = 1;
            lcd_fill(152, 0, 8, 8, 0x07E0);      /* 右上绿点：已联机 */
            usb_write(f, 6);
        }
        return;
    }

    if (cls == 0x02u) {
        switch (sub) {
        case 0x00:
            x0 = a;
            y0 = b;
            return;
        case 0x01:
            x1 = a;
            y1 = b;
            return;
        case 0x02:
            page_no = (uint8_t)a;
            return;
        case 0x03:
            switch (f[2]) {
            case 0x00:
                lcd_fill(x0, y0, x1 - x0, y1 - y0, (uint16_t)((f[3] << 8) | f[4]));
                return;
            case 0x04:                            /* 纯色填充，参数与上面相同 */
                lcd_fill(x0, y0, x1 - x0, y1 - y0, (uint16_t)((f[3] << 8) | f[4]));
                return;
            case 0x07:
                t_frame_start = systick_now();
                lcd_stream_begin(x0, y0, x1, y1);
                return;
            case 0x08:
                raw_bytes_seen += (uint32_t)((f[3] << 8) | f[4]);
                return;
            case 0x0Cu:
                if (stage_len > 0u) { raw_bytes_seen += stage_len; stage_len = 0u; }
                raw_left = (uint32_t)((f[3] << 8) | f[4]);
                return;
            case 0x09: {
                uint8_t st[6];
                if (stage_len > 0u) { raw_bytes_seen += stage_len; stage_len = 0u; }
                lcd_raw_idle();
                frame_done++;
                last_frame_ms = (uint16_t)((systick_now() - t_frame_start) / 48000u);
                if (debug_on) {
                    st[0] = 0x55;
                    st[1] = (uint8_t)(frame_done & 0xFFu);
                    st[2] = (uint8_t)((frame_done >> 8) & 0xFFu);
                    st[3] = (uint8_t)(raw_bytes_seen / 256u); raw_bytes_seen = 0u;
                    st[4] = (uint8_t)((last_frame_ms >> 8) & 0xFFu);
                    st[5] = 0xAA;
                    usb_write(st, 6);
                }
                return;
            }
            default:
                return;
            }
        case 0x04:
            seg_fill4(&f[2]);
            return;
        default:
            return;
        }
    }

    if (cls == 0x04u) {
        uint32_t base = (uint32_t)f[1] * 4u;
        if (base + 4u <= 256u) {
            seg[base]     = f[2];
            seg[base + 1] = f[3];
            seg[base + 2] = f[4];
            seg[base + 3] = f[5];
        }
        return;
    }

    if (cls == 0x08u) {
        uint8_t reply[6];
        uint16_t v = 0;                            /* ADC 尚未接入 */
        reply[0] = 0x08;
        reply[1] = f[1];
        reply[2] = 0;
        reply[3] = 0;
        reply[4] = (uint8_t)(v >> 8);
        reply[5] = (uint8_t)v;
        usb_write(reply, 6);
        return;
    }
}

int main(void)
{
    uint32_t idle = 0;
    int rx_len = 0;

    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;
    board_clock_init();
    lcd_gpio_init();
    lcd_init();
    lcd_fill(0, 0, 160, 80, 0x0000);
    lcd_fill(0, 0, 8, 80, 0xFFFF);        /* 左白边：固件在跑 */

    usb_init();

    /* SysTick 自检：10 ms 应当约 480 000 个计数 */
    {
        uint32_t a, b, d;
        uint8_t c[6];
        systick_init();
        a = systick_now();
        delay_ms(10);
        b = systick_now();
        d = (b - a) / 48000u;      /* 折算成毫秒，应当接近 10 */
        c[0] = 0x77u;
        c[1] = (uint8_t)(d & 0xFFu);
        c[2] = (uint8_t)((d >> 8) & 0xFFu);
        c[3] = 0;
        c[4] = 0;
        c[5] = 0xAAu;
        usb_write(c, 6);
    }

    for (;;) {
        int n;
        static uint32_t quiet;
        static uint32_t hello_tick;

        while (rx_len < (int)sizeof(rx)) {
            n = usb_read(rx + rx_len, (int)sizeof(rx) - rx_len);
            if (n <= 0)
                break;
            rx_len += n;
            quiet = 0u;
            hello_tick = 0u;
        }

        if (rx_len > 0) {
            int pos = 0;

            while (pos < rx_len) {
                if (raw_left > 0u) {
                    uint32_t take = (uint32_t)(rx_len - pos);
                    uint32_t room = (uint32_t)sizeof(stage) - stage_len;
                    uint32_t i;

                    if (take > raw_left)
                        take = raw_left;
                    if (take > room)
                        take = room;
                    for (i = 0; i < take; i++)
                        stage[stage_len + i] = rx[pos + (int)i];

                    stage_len += take;
                    pos += (int)take;
                    raw_left -= take;

                    if (stage_len == sizeof(stage) || (raw_left == 0u && stage_len > 0u)) {
                        raw_bytes_seen += stage_len;
                        stage_len = 0u;
                    }
                    continue;
                }

                if (rx_len - pos >= 6) {
                    handle_frame(rx + pos);
                    pos += 6;
                    continue;
                }
                break;
            }

            if (pos > 0) {
                int k;
                for (k = pos; k < rx_len; k++)
                    rx[k - pos] = rx[k];
                rx_len -= pos;
            }
        }

        /* 串口安静一段时间就周期性打招呼：主机的握手只在这时能被满足，
         * 一旦有数据进来立刻停，避免握手包混进图像流。 */
        if (++quiet >= 30000u) {
            if (hello_tick == 0u)
                usb_write(hello, 6);
            if (++hello_tick >= 150000u)
                hello_tick = 0u;
        }
    }
}
