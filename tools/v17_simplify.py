"""v17: 去掉栅栏/死亡锁这一整套（它是这一轮花屏的元凶），只保留
    - 每帧一个裁决字节（0x06 收齐 / 0x15 丢了）
    - 400 ms 没有进展就作废当前段并发 0x15（不锁死）
    - 环溢出只作废当前帧（不锁死）
    恢复 proto25 的 NAK 水位（256/2048），让设备宁可让主机等也不要丢字节。

用法：python v17_simplify.py            # 打补丁
      python v17_simplify.py --check    # 只检查
"""
import io
import sys

SRC = r"C:\Users\hsush\Desktop\MSU2_MINI\firmware\src"
PROTO = SRC + r"\main_proto.c"
USB = SRC + r"\usb.c"
FWH = SRC + r"\fw.h"

dry = "--check" in sys.argv


def sub(text, old, new, tag):
    n = text.count(old)
    if n != 1:
        raise SystemExit("[FAIL] %s: found %d times\n---\n%s" % (tag, n, old))
    return text.replace(old, new, 1)


proto = io.open(PROTO, encoding="utf-8").read()

# 1) 删掉 FENCE / stream_dead 定义
proto = sub(proto, """/* 同步栅栏：主机发现坏帧时发这 6 个字节，设备无论在收像素还是在
   等命令，都要认出来并把现场清干净。像素里出现这个序列的概率是
   2^-48，一帧 25 KB 也只有 1e-10，可以放心当同步字。 */
static const uint8_t FENCE[6] = {0xA5u, 0x5Au, 0xA5u, 0x5Au, 0x0Eu, 0x07u};
static uint8_t fence_n;
static uint8_t stream_dead;   /* 1 = 丢弃一切，直到看见栅栏 */
""", "", "fence-decl")

proto = sub(proto, """    rle_n = 0u;
    fence_n = 0u;
}""", """    rle_n = 0u;
}""", "stream_reset")

proto = sub(proto, """static void fence_ack(void)
{
    uint8_t ack = 0x07u;   /* 栅栏回执用 0x07，别和帧裁决 0x06 混 */
    usb_write(&ack, 1u);
}

""", "", "fence_ack")

proto = sub(proto, """/* 逐字节匹配栅栏，返回 1 表示一个完整栅栏刚刚收完。 */
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

""", "", "fence_scan")

# 2) 段看门狗：作废当前段，但不进入死亡锁
proto = sub(proto, """                stream_timeouts++;
                stream_dead = 1u;
                dead_idle = 0u;
                usb_write(&err, 1u);""",
            """                stream_timeouts++;
                usb_write(&err, 1u);""", "watchdog-dead")

# 3) 环溢出：只作废本帧
proto = sub(proto, """                stream_aborts++;
                stream_dead = 1u;
                usb_write(&err, 1u);""",
            """                stream_aborts++;
                usb_write(&err, 1u);""", "overflow-dead")

# 4) stall 兜底也不再锁死
proto = sub(proto, """                lcd_raw_idle();
                stream_dead = 1u;
                stall = 0u;""",
            """                lcd_raw_idle();
                stall = 0u;""", "stall-dead")

# 5) 删掉整个 stream_dead 分支
start = proto.index("        if (stream_dead) {")
end = proto.index("        } else if (rx_len > 0) {", start)
proto = proto[:start] + "        if (rx_len > 0) {" + proto[end + len("        } else if (rx_len > 0) {"):]

# 6) 删掉收像素时的栅栏扫描与 jam 处理
proto = sub(proto, """                    uint32_t i;
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
""", """                    uint32_t i;

                    if (take > raw_left)
                        take = raw_left;
""", "fence-in-pixels")

proto = sub(proto, """                    if (jam) {
                        stream_reset();
                        stage_len = 0u;
                        stage_idx = 0u;
                        lcd_raw_idle();
                        stream_dead = 0u;
                        fence_ack();
                        pos += 1;              /* 跳过栅栏最后一个字节 */
                        continue;
                    }
""", "", "jam-block")

# 7) 新帧开头把“进展”时间戳对齐，避免看门狗误杀
proto = sub(proto, """            case 0x07:
                t_frame_start = systick_now();
                raw_left = 0u;                        /* 新帧开头强制对齐 */""",
            """            case 0x07:
                t_frame_start = systick_now();
                last_progress = t_frame_start;
                last_raw_left = 0xFFFFFFFFu;
                raw_left = 0u;                        /* 新帧开头强制对齐 */""", "case07-progress")

io.open(PROTO, "w", encoding="utf-8", newline="\n").write(proto)
print("[ok] main_proto.c %d -> %d bytes" % (0, len(proto.encode("utf-8"))))

# usb.c：NAK 水位恢复到 256 / 2048
usb = io.open(USB, encoding="utf-8").read()
usb = sub(usb, "if (free_bytes < 4096u) {", "if (free_bytes < 256u) {", "nak-low")
usb = sub(usb, "if (free_bytes >= 4096u) {", "if (free_bytes >= 2048u) {", "nak-high")
io.open(USB, "w", encoding="utf-8", newline="\n").write(usb)
print("[ok] usb.c")

# 版本号 17
fwh = io.open(FWH, encoding="utf-8").read()
fwh = sub(fwh, "#define FW_VER_MAJOR  16u", "#define FW_VER_MAJOR  17u", "ver")
io.open(FWH, "w", encoding="utf-8", newline="\n").write(fwh)
print("[ok] fw.h -> 17.0")
print("done")