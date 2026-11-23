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
static uint8_t ver_pending;
static uint8_t ver_val;
static uint32_t raw_left;                 /* 还剩多少原始像素字节 */
static uint8_t stage[2][512];
static uint8_t stage_idx;
static uint32_t stage_len;
static uint8_t rle_mode;                 /* 0=原样 1=行程编码 */
static uint8_t rle_acc[3];
static uint8_t rle_n;

static uint32_t frame_px;
static uint8_t standby_mode = 1u;
static uint8_t blink_first = 1u;      /* 待机画面还没画过 */
static uint8_t blink_on = 1u;         /* 当前方块颜色 1=白 0=黄 */
static uint32_t idle_ms;              /* 没收到任何字节的粗略毫秒数 */
static uint32_t win_px;   /* 当前流的窗口像素数，用于整帧角标判定 */

static void stage_flush(void)
{
    if (stage_len > 0u) {
        frame_px += (uint32_t)(stage_len >> 1);
        lcd_raw_write(stage[stage_idx], stage_len);
        stage_len = 0u;
        stage_idx ^= 1u;
    }
}

/* 行程编码：每三个字节一组 [像素数, 高字节, 低字节]，展开后进 stage。 */
static void rle_feed(uint8_t b)
{
    rle_acc[rle_n++] = b;
    if (rle_n < 3u)
        return;
    rle_n = 0u;
    {
        uint32_t cnt = rle_acc[0];
        uint8_t hi = rle_acc[1];
        uint8_t lo = rle_acc[2];

        while (cnt-- > 0u) {
            if (stage_len + 2u > (uint32_t)sizeof(stage[0]))
                stage_flush();
            stage[stage_idx][stage_len++] = hi;
            stage[stage_idx][stage_len++] = lo;
        }
    }
}
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
        if (sub == 0x4Du) {
            connected = 1;
            usb_write(f, 6);
            return;
        }
        if (sub == 0x56u) {                       /* 版本查询：不主动发送，只应答 */
            uint8_t st[6];
            uint16_t tv = adc_read(9u);
            st[0] = 0x56u;
            st[1] = FW_VER_MAJOR;
            st[2] = FW_VER_MINOR;
            st[3] = (uint8_t)((tv >> 8) & 0xFFu);
            st[4] = (uint8_t)(tv & 0xFFu);
            st[5] = 0xAAu;
            usb_write(st, 6);
            return;
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
                standby_mode = 0u;
                win_px = (x1 > x0 && y1 > y0)
                       ? (uint32_t)(x1 - x0) * (uint32_t)(y1 - y0) : 0u;
                lcd_stream_begin(x0, y0, x1, y1);
                return;
            case 0x08:
                lcd_raw_write(seg, (uint32_t)((f[3] << 8) | f[4]));
                return;
            case 0x0Du:                          /* 压缩段：f[5]=1 行程编码 */
                stage_flush();
                rle_mode = f[5];
                rle_n = 0u;
                raw_left = (uint32_t)((f[3] << 8) | f[4]);
                return;
            case 0x0Cu:
                if (stage_len > 0u) { lcd_raw_write(stage[stage_idx], stage_len); stage_len = 0u; stage_idx ^= 1u; }
                rle_mode = 0u;               /* 原样段：必须关掉上一段的行程解码 */
                rle_n = 0u;
                raw_left = (uint32_t)((f[3] << 8) | f[4]);
                return;
            case 0x09: {
                uint32_t frame_px_before = frame_px;
                uint32_t win_px_before = win_px;
                if (stage_len > 0u) { lcd_raw_write(stage[stage_idx], stage_len); stage_len = 0u; stage_idx ^= 1u; }
                lcd_raw_idle();
                win_px = 0u;      /* 完整性色块已移除：像素对不对由裁决字节告诉上位机 */
                frame_px = 0u;
                frame_done++;
                last_frame_ms = (uint16_t)((systick_now() - t_frame_start) / 48000u);
                /* 每帧一个裁决字节：0x06 = 像素数对得上，0x15 = 少收了。
                 * 主机读它就能“发一帧等一次”，天然限速，永远不把接收环挤爆。 */
                ver_val = (uint8_t)((win_px_before == 0u || frame_px_before == win_px_before)
                                    ? 0x06u : 0x15u);
                ver_pending = 1u;
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
        uint16_t v = adc_read(f[1]);                            /* ADC 尚未接入 */
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


/* --- 独立看门狗：主循环卡死时自动复位，不用再拔插 USB --- */
static void iwdg_start(void)
{
    *(volatile uint32_t *)0x40003000u = 0x5555u;   /* 解锁 PR/RL */
    *(volatile uint32_t *)0x40003004u = 0x0003u;   /* LSI/16 ≈ 1.6 s */
    *(volatile uint32_t *)0x40003008u = 0x0FFFu;
    *(volatile uint32_t *)0x40003000u = 0xCCCCu;   /* 启动 */
    *(volatile uint32_t *)0x40003000u = 0xAAAAu;   /* 喂一次 */
}

static void iwdg_feed(void)
{
    *(volatile uint32_t *)0x40003000u = 0xAAAAu;
}

int main(void)
{
    int rx_len = 0;

    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;
    board_clock_init();
    lcd_gpio_init();
    lcd_init();
    lcd_fill(0, 0, 160, 80, 0x0000);
    lcd_fill(0, 0, 8, 8, 0xFFFF);         /* 四个角各一个方块：待机时白/黄闪 */
    lcd_fill(152, 0, 8, 8, 0xFFFF);
    lcd_fill(0, 72, 8, 8, 0xFFFF);
    lcd_fill(152, 72, 8, 8, 0xFFFF);

    adc_init();      /* 先把 ADC 弄好，免得刚枚举完就被阻住 */
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

    iwdg_start();

    for (;;) {
        iwdg_feed();
        int n;
        static uint32_t quiet;
        static uint32_t hello_tick;
        static uint32_t stall;

        while (rx_len < (int)sizeof(rx)) {
            n = usb_read(rx + rx_len, (int)sizeof(rx) - rx_len);
            if (n <= 0)
                break;
            rx_len += n;
            quiet = 0u;
            hello_tick = 0u;
            stall = 0u;
            idle_ms = 0u;
        }

        /* 原始像素流中途断掉时放弃本段：否则剩下的字会被当成像素，
         * 之后的帧头全部错位，表现为画面冻结且切插件也无法恢复。 */
        if (raw_left > 0u && rx_len == 0) {
            if (++stall > 4000u) {
                raw_left = 0u;
                stage_len = 0u;
                rle_n = 0u;
                lcd_raw_idle();
                stall = 0u;
            }
        }

        /* 上位机断开（或一直没发帧）超过 4 秒，就回到待机画面，
         * 免得屏幕永远停在最后一帧上。计时用 delay_ms(1) 累加，
         * 空闲时主循环本来也没事可做，阻塞 1 ms 不影响任何东西。 */
        if (rx_len == 0 && raw_left == 0u) {
            if (idle_ms < 6000u)
                idle_ms++;
        } else {
            idle_ms = 0u;
        }

        if (standby_mode == 0u && idle_ms >= 4000u) {
            uint32_t k;

            standby_mode = 1u;
            blink_first = 1u;
            blink_on = 1u;
            stage_len = 0u;
            stage_idx = 0u;
            rle_n = 0u;
            frame_px = 0u;
            lcd_raw_idle();
            lcd_fill(0, 0, 160, 80, 0x0000);
            lcd_fill(0, 0, 8, 8, 0xFFFFu);
            lcd_fill(152, 0, 8, 8, 0xFFFFu);
            lcd_fill(0, 72, 8, 8, 0xFFFFu);
            lcd_fill(152, 72, 8, 8, 0xFFFFu);
            for (k = 0u; k < 6u; k++)
                iwdg_feed(), delay_ms(1);
        }

        /* 待机画面：正中 8x8 方块，白/黄每秒交替一次。主机一开流就停，
         * 免得它跟画面抢窗口。
         *
         * 计时不用 SysTick（它的计数方向/分频都不直观，之前算出过纯白），
         * 而是老老实实数 1000 个 delay_ms(1)：delay_ms 是实测标定的
         * “3000 次空循环约 1 ms”，一秒很稳。等待期间照样喂狗、照样收字节，
         * 一有数据立刻结束等待回去处理协议，不会漏包。 */
        if (standby_mode) {
            uint32_t i;

            if (blink_first) {
                blink_first = 0u;
                lcd_fill(76, 36, 8, 8, 0xFFFFu);
            } else {
                for (i = 0u; i < 600u; i++) {
                    iwdg_feed();
                    n = usb_read(rx + rx_len, (int)sizeof(rx) - rx_len);
                    if (n > 0) {
                        rx_len += n;
                        quiet = 0u;
                        hello_tick = 0u;
                        stall = 0u;
                        break;
                    }
                    /* 待机时主循环几乎不转，打招呼的快计数器涨不起来，
                     * 主机就会“找不到设备”。这里每约 100 拍补一次 hello。 */
                    if ((i % 50u) == 0u)
                        usb_write(hello, 6);
                    delay_ms(1);
                }
                if (rx_len == 0) {
                    uint16_t col;

                    blink_on = (uint8_t)(blink_on ^ 1u);
                    col = blink_on ? 0xFFFFu : 0xFFE0u;
                    lcd_fill(0, 0, 8, 8, col);        /* 左上 */
                    lcd_fill(152, 0, 8, 8, col);      /* 右上 */
                    lcd_fill(0, 72, 8, 8, col);       /* 左下 */
                    lcd_fill(152, 72, 8, 8, col);     /* 右下 */
                }
            }
        }

        if (rx_len > 0) {
            int pos = 0;

            while (pos < rx_len) {
                if (raw_left > 0u) {
                    uint32_t take = (uint32_t)(rx_len - pos);
                    uint32_t i;

                    if (take > raw_left)
                        take = raw_left;

                    if (rle_mode) {
                        for (i = 0; i < take; i++)
                            rle_feed((uint8_t)rx[pos + (int)i]);
                        pos += (int)take;
                        raw_left -= take;
                        if (raw_left == 0u)
                            stage_flush();
                    } else {
                        uint32_t room = (uint32_t)sizeof(stage[0]) - stage_len;

                        if (take > room)
                            take = room;
                        for (i = 0; i < take; i++)
                            stage[stage_idx][stage_len + i] = rx[pos + (int)i];

                        stage_len += take;
                        pos += (int)take;
                        raw_left -= take;

                        if (stage_len == sizeof(stage[0]) || (raw_left == 0u && stage_len > 0u))
                            stage_flush();
                    }
                    stall = 0u;
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
        usb_pump();
        if (ver_pending && usb_write(&ver_val, 1u))
            ver_pending = 0u;

        if (++quiet >= 30000u) {
            if (hello_tick == 0u)
                usb_write(hello, 6);
            if (++hello_tick >= 150000u)
                hello_tick = 0u;
        }

        /* 空闲时按 1 ms 一拍走，给 idle_ms 一个真实的时间基准；
         * 有数据时保持全速，不拖慢收流。 */
        if (rx_len == 0 && raw_left == 0u)
            delay_ms(1);
    }
}
