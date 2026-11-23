# -*- coding: utf-8 -*-
"""v14：绝对流期限内自愈 + 每个 0x09 回 1 字节裁决（0x06 好 / 0x15 坏）。"""
import io, sys

P = r'C:\Users\hsush\Desktop\MSU2_MINI\firmware\src\main_proto.c'
s = io.open(P, encoding='utf-8').read()
before = len(s)

def sub(old, new, why, count=1):
    global s
    if old not in s:
        print('MISSING:', why); sys.exit(1)
    assert s.count(old) == count, 'expected %d, got %d: %s' % (count, s.count(old), why)
    s = s.replace(old, new)

# 0) 新变量
sub("static uint32_t stream_watch;       /* 最近一次推进段的系统节拍 */",
    "static uint32_t stream_watch;       /* 最近一次推进段的系统节拍 */\n"
    "static uint32_t stream_start;       /* 本次流开始的节拍（绝对期限用） */",
    'stream_start decl')

# 1) 所有推进段的地方同时记开始时刻
sub("stream_watch = systick_now();",
    "stream_watch = systick_now(); stream_start = stream_watch;",
    'touch stream_start', count=s.count("stream_watch = systick_now();"))

# 2) 绝对期限替换“收字节就续命”的 60 ms 规则
sub(
"""        if (raw_left > 0u || seg_mode != 0u) {
            if ((uint32_t)(systick_now() - stream_watch) > 2880000u) {   /* 60 ms @ 48 MHz */
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
            }
        }""",
"""        if (raw_left > 0u || seg_mode != 0u) {
            /* 一段流最多活 200 ms（整屏 26 KB 约 90 ms，给一倍余量）。
               期限是绝对的，不随“又收到几个字节”续命 —— 否则主机停在
               半路、之后还一直发别的字节时，设备会永远等下去，把后面
               所有命令都当像素吃掉，只能拔电重启。 */
            if ((uint32_t)(systick_now() - stream_start) > 9600000u) {
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
            }
        }""",
    'absolute deadline')

# 3) 每个 0x09 回一个裁决字节；调试模式下仍发 18 字节详情
sub(
"""                    if (debug_on || win >= 12800u) {
                        st[0] = 0x55;""",
"""                    if (!debug_on) {
                        uint8_t verdict = (win == 0u || px == win) ? 0x06u : 0x15u;
                        usb_write(&verdict, 1u);
                    }
                    if (debug_on) {
                        st[0] = 0x55;""",
    'verdict byte')

io.open(P, 'w', encoding='utf-8', newline='\n').write(s)
print('patched %d -> %d bytes' % (before, len(s)))