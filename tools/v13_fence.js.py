# -*- coding: utf-8 -*-
"""v13：设备端加“同步栅栏 + 死亡锁”，彻底切断坏帧之后的命令误解析。"""
import io, sys

P = r'C:\Users\hsush\Desktop\MSU2_MINI\firmware\src\main_proto.c'
s = io.open(P, encoding='utf-8').read()
orig = s
def sub(old, new, why):
    global s
    if old not in s:
        print('MISSING:', why); sys.exit(1)
    assert s.count(old) == 1, 'ambiguous: ' + why
    s = s.replace(old, new, 1)

# 1) 全局：死亡锁 + 栅栏匹配器
sub(
"""static uint32_t frame_px;
static uint32_t win_px;      /* 本次流要覆盖的像素数（分块时小于整屏） */
""",
"""static uint32_t frame_px;
static uint32_t win_px;      /* 本次流要覆盖的像素数（分块时小于整屏） */

/* 同步栅栏：主机发现坏帧时发这 6 个字节，设备无论在收像素还是在
   等命令，都要认出来并把现场清干净。像素里出现这个序列的概率是
   2^-48，一帧 25 KB 也只有 1e-10，可以放心当同步字。 */
static const uint8_t FENCE[6] = {0xA5u, 0x5Au, 0xA5u, 0x5Au, 0x0Eu, 0x07u};
static uint8_t fence_n;
static uint8_t stream_dead;   /* 1 = 丢弃一切，直到看见栅栏 */""",
    'globals')

# 2) 辅助函数
sub(
"""static void stage_flush(void)
{""",
"""static void stream_reset(void)
{
    raw_left = 0u;
    seg_mode = 0u;
    rle_n = 0u;
    fence_n = 0u;
}

static void fence_ack(void)
{
    uint8_t ack = 0x06u;
    usb_write(&ack, 1u);
}

/* 逐字节匹配栅栏，返回 1 表示一个完整栅栏刚刚收完。 */
static uint8_t fence_scan(uint8_t b)
{
    if (b == FENCE[fence_n]) {
        if (++fence_n >= 6u) {
            fence_n = 0u;
            return 1u;
        }
        return 0u;
    }
    fence_n = (b == FENCE[0]) ? 1u : 0u;
    return 0u;
}

static void stage_flush(void)
{""",
    'helpers')

# 3) case 0x09：先把 win 存下来再清，角标与状态帧都用存下来的值
sub(
"""                if (win_px >= 12800u) {
                    lcd_fill(152, 72, 8, 8, (frame_px == win_px) ? 0x07E0u : 0xF800u);
                    win_px = 0u;
                }
                {
                    uint32_t px = frame_px;
                    uint32_t win = win_px;
""",
"""                {
                    uint32_t px = frame_px;
                    uint32_t win = win_px;

                    if (win >= 12800u)
                        lcd_fill(152, 72, 8, 8, (px == win) ? 0x07E0u : 0xF800u);
                    win_px = 0u;
""",
    'case09 order')

# 4) 段超时：进死亡锁
sub(
"""            if ((uint32_t)(systick_now() - stream_watch) > 2880000u) {   /* 60 ms @ 48 MHz */
                raw_left = 0u;
                seg_mode = 0u;
                rle_n = 0u;
                stage_len = 0u;
                lcd_raw_idle();
                stream_timeouts++;
                rx_len = 0;
                usb_flush_rx();
            }""",
"""            if ((uint32_t)(systick_now() - stream_watch) > 2880000u) {   /* 60 ms @ 48 MHz */
                uint8_t err = 0x15u;
                stream_reset();
                stage_len = 0u;
                stage_idx = 0u;
                lcd_raw_idle();
                stream_timeouts++;
                stream_dead = 1u;
                usb_write(&err, 1u);          /* 让主机立刻知道这一帧废了 */
                rx_len = 0;
                usb_flush_rx();
            }""",
    'timeout dead')

# 5) 环溢出：同样进死亡锁
sub(
"""            if (raw_left > 0u || seg_mode != 0u) {
                raw_left = 0u;
                seg_mode = 0u;
                rle_n = 0u;
                stage_len = 0u;
                lcd_raw_idle();
                stream_aborts++;
            }
            rx_len = 0;
            usb_flush_rx();""",
"""            if (raw_left > 0u || seg_mode != 0u) {
                uint8_t err = 0x15u;
                stream_reset();
                stage_len = 0u;
                stage_idx = 0u;
                lcd_raw_idle();
                stream_aborts++;
                stream_dead = 1u;
                usb_write(&err, 1u);
            }
            rx_len = 0;
            usb_flush_rx();""",
    'overflow dead')

# 6) 主机半路停发：也进死亡锁，等栅栏
sub(
"""        if (raw_left > 0u && rx_len == 0) {
            if (++stall > 4000u) {
                raw_left = 0u;
                stage_len = 0u;
                rle_n = 0u;
                seg_mode = 0u;
                lcd_raw_idle();
                stall = 0u;
            }
        }""",
"""        if (raw_left > 0u && rx_len == 0) {
            if (++stall > 4000u) {
                stream_reset();
                stage_len = 0u;
                stage_idx = 0u;
                lcd_raw_idle();
                stream_dead = 1u;
                stall = 0u;
            }
        }""",
    'stall dead')

# 7) 收像素时先找栅栏：找到就只消费栅栏之前的部分，然后复位
sub(
"""            while (pos < rx_len) {
                if (raw_left > 0u) {
                    stream_watch = systick_now();
                    uint32_t take = (uint32_t)(rx_len - pos);
                    uint32_t i;

                    if (take > raw_left)
                        take = raw_left;
""",
"""            while (pos < rx_len) {
                if (raw_left > 0u) {
                    stream_watch = systick_now();
                    uint32_t take = (uint32_t)(rx_len - pos);
                    uint32_t i;
                    uint8_t jam = 0u;

                    if (take > raw_left)
                        take = raw_left;

                    for (i = 0; i < take; i++) {
                        if (fence_scan((uint8_t)rx[pos + (int)i])) {
                            take = i;
                            jam = 1u;
                            break;
                        }
                    }
""",
    'stream fence scan')

# 8) 各分支消费完之后处理 jam
sub(
"""                    stall = 0u;
                    continue;
                }

                if (rx_len - pos >= 6) {""",
"""                    stall = 0u;
                    if (jam) {
                        stream_reset();
                        stage_len = 0u;
                        stage_idx = 0u;
                        lcd_raw_idle();
                        stream_dead = 0u;
                        fence_ack();
                        pos += 1;              /* 跳过栅栏最后一个字节 */
                        continue;
                    }
                    continue;
                }

                if (rx_len - pos >= 6) {""",
    'stream jam handle')

# 9) 死亡锁：只认栅栏，其余字节全丢
sub(
"""        if (rx_len > 0) {
            int pos = 0;
""",
"""        if (stream_dead) {
            int k;
            for (k = 0; k < rx_len; k++) {
                if (fence_scan(rx[k])) {
                    stream_dead = 0u;
                    stream_reset();
                    stage_len = 0u;
                    stage_idx = 0u;
                    lcd_raw_idle();
                    fence_ack();
                    break;
                }
            }
            rx_len = 0;
        } else if (rx_len > 0) {
            int pos = 0;
""",
    'dead gate')

io.open(P, 'w', encoding='utf-8', newline='\n').write(s)
print('patched, %d -> %d bytes' % (len(orig), len(s)))