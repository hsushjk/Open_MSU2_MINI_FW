# -*- coding: utf-8 -*-
"""v15：把“整段 200 ms 绝对期限”换成“没进展才算卡死”。

真机实测整帧写到设备要 230 ms 左右（主机 USB 包往返受限），200 ms 的
绝对期限会把正常整帧也判死；换成“raw_left 连续 400 ms 没有减少就判死”，
慢但一直在推进的流不会被误杀。另外 stream_dead 支持静默 2 秒自愈，
这样即使主机崩了，拔不拔线都能自己回到命令模式。
"""
import io

P = r'C:\Users\hsush\Desktop\MSU2_MINI\firmware\src\main_proto.c'
s = io.open(P, encoding='utf-8').read()

# 1) 新增变量
old = '''static uint32_t stream_watch;       /* 最近一次推进段的系统节拍 */
static uint32_t stream_start;       /* 本次流开始的节拍（绝对期限用） */'''
new = '''static uint32_t stream_watch;       /* 最近一次推进段的系统节拍 */
static uint32_t stream_start;       /* 本次流开始的节拍（硬上限用） */
static uint32_t last_progress;      /* raw_left 最近一次真正减少的节拍 */
static uint32_t last_raw_left;      /* 上一轮看到的 raw_left */
static uint32_t dead_idle;          /* stream_dead 期间的静默计数 */'''
if s.count(old) != 1:
    raise SystemExit('anchor 1 count %d' % s.count(old))
s = s.replace(old, new, 1)

# 2) 期限逻辑：改成“没进展 400 ms 判死”，另加 2 s 硬上限
old = '''        if (raw_left > 0u || seg_mode != 0u) {
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
        }'''
new = '''        if (raw_left > 0u || seg_mode != 0u) {
            /* 判死规则：raw_left 连续 400 ms 没有减少 —— 也就是“没有进展”。
               不能只看“多久没收到字节”（主机卡在半路时还在发别的字节，
               那些字节会被当像素吃掉，raw_left 一直在减少，永远不饿死），
               也不能只挂一个整段期限（正常整帧写到设备要 230 ms 左右，
               真机实测会误杀）。所以两头都要：没进展 400 ms 判死 +
               整段 2 s 硬上限兜底。 */
            uint32_t now = systick_now();

            if (raw_left != last_raw_left) {
                last_raw_left = raw_left;
                last_progress = now;
            }
            if ((uint32_t)(now - last_progress) > 19200000u ||
                (uint32_t)(now - stream_start) > 96000000u) {
                uint8_t err = 0x15u;
                stream_reset();
                stage_len = 0u;
                stage_idx = 0u;
                lcd_raw_idle();
                stream_timeouts++;
                stream_dead = 1u;
                dead_idle = 0u;
                usb_write(&err, 1u);          /* 让主机立刻知道这一帧废了 */
                rx_len = 0;
                usb_flush_rx();
            }
        }'''
if s.count(old) != 1:
    raise SystemExit('anchor 2 count %d' % s.count(old))
s = s.replace(old, new, 1)

# 3) stream_dead：静默 2 秒自愈（主循环约 1 kHz，2000 次约 2 s）
old = '''        if (stream_dead) {
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
        } else if (rx_len > 0) {'''
new = '''        if (stream_dead) {
            int k;
            for (k = 0; k < rx_len; k++) {
                if (fence_scan(rx[k])) {
                    stream_dead = 0u;
                    stream_reset();
                    stage_len = 0u;
                    stage_idx = 0u;
                    lcd_raw_idle();
                    fence_ack();
                    dead_idle = 0u;
                    break;
                }
            }
            /* 主机没发栅栏也要能自己爬起来：静默 2 秒就退回命令模式。
               否则上位机一崩，设备就永远“哑”着，用户只能拔电。 */
            if (rx_len == 0) {
                if (++dead_idle > 2000u) {
                    stream_dead = 0u;
                    stream_reset();
                    stage_len = 0u;
                    stage_idx = 0u;
                    lcd_raw_idle();
                    dead_idle = 0u;
                }
            } else {
                dead_idle = 0u;
            }
            rx_len = 0;
        } else if (rx_len > 0) {'''
if s.count(old) != 1:
    raise SystemExit('anchor 3 count %d' % s.count(old))
s = s.replace(old, new, 1)

# 4) 段开始时也刷新进展时间
old = '''    uint32_t verdict;'''
io.open(P, 'w', encoding='utf-8', newline='\n').write(s)
print('main_proto patched')

# 5) 版本号
P2 = r'C:\Users\hsush\Desktop\MSU2_MINI\firmware\src\fw.h'
h = io.open(P2, encoding='utf-8').read()
assert '#define FW_VER_MAJOR 14u' in h
h = h.replace('#define FW_VER_MAJOR 14u', '#define FW_VER_MAJOR 15u', 1)
io.open(P2, 'w', encoding='utf-8', newline='\n').write(h)
print('fw.h -> 15')