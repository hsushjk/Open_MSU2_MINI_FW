// 给 main_proto.c 加 4 位索引色：调色板命令 0x0F、索引段命令 0x0E
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
let s = fs.readFileSync(P, 'utf8');
const crlf = s.includes('\r\n');
s = s.replace(/\r\n/g, '\n');

function must(find, repl, label) {
    if (!s.includes(find)) { console.error('NOT FOUND: ' + label); process.exit(1); }
    s = s.replace(find, repl);
    console.log('ok: ' + label);
}

// 1) 变量声明
must(
`static uint8_t rle_mode;                 /* 0=原样 1=行程编码 */`,
`static uint16_t pal[16];                 /* 4 位索引色的调色板（RGB565） */
static uint8_t seg_mode;                 /* 0=原样 1=行程 2=调色板 3=4 位索引 */
static uint8_t rle_mode;                 /* 兼容旧路径：0=原样 1=行程编码 */`,
'declarations');

// 2) 两个 feed 函数，插在 rle_feed 之后
must(
`            stage[stage_idx][stage_len++] = hi;
            stage[stage_idx][stage_len++] = lo;
        }
    }
}
static uint32_t t_frame_start;`,
`            stage[stage_idx][stage_len++] = hi;
            stage[stage_idx][stage_len++] = lo;
        }
    }
}

/* 调色板：每两字节一个 RGB565（高字节在前），共 32 字节 / 16 色。 */
static void pal_feed(uint8_t b)
{
    uint8_t i = (uint8_t)(pal_n >> 1);

    if (i < 16u) {
        if ((pal_n & 1u) == 0u)
            pal[i] = (uint16_t)((uint16_t)b << 8);
        else
            pal[i] = (uint16_t)(pal[i] | (uint16_t)b);
    }
    pal_n++;
}

/* 4 位索引：一个字节两个像素，按调色板展开成两个 RGB565 像素。 */
static void idx4_feed(uint8_t b)
{
    uint16_t c0 = pal[(uint8_t)((b >> 4) & 0x0Fu)];
    uint16_t c1 = pal[(uint8_t)(b & 0x0Fu)];

    if (stage_len + 4u > (uint32_t)sizeof(stage[0]))
        stage_flush();
    stage[stage_idx][stage_len++] = (uint8_t)(c0 >> 8);
    stage[stage_idx][stage_len++] = (uint8_t)(c0 & 0xFFu);
    stage[stage_idx][stage_len++] = (uint8_t)(c1 >> 8);
    stage[stage_idx][stage_len++] = (uint8_t)(c1 & 0xFFu);
}
static uint32_t t_frame_start;`,
'feed functions');

// 3) 声明 pal_n
must(
`static uint8_t rle_acc[3];`,
`static uint8_t pal_n;
static uint8_t rle_acc[3];`,
'pal_n');

// 4) 命令分支
must(
`            case 0x0Cu:
                if (stage_len > 0u) { lcd_raw_write(stage[stage_idx], stage_len); stage_len = 0u; stage_idx ^= 1u; }
                rle_mode = 0u;               /* 原样段：必须关掉上一段的行程解码 */`,
`            case 0x0Fu:                          /* 调色板：32 字节，f[3]:f[4] = 长度 */
                stage_flush();
                seg_mode = 2u;
                pal_n = 0u;
                raw_left = (uint32_t)((f[3] << 8) | f[4]);
                return;
            case 0x0Eu:                          /* 4 位索引段：一字节两个像素 */
                stage_flush();
                rle_mode = 0u;
                seg_mode = 3u;
                raw_left = (uint32_t)((f[3] << 8) | f[4]);
                return;
            case 0x0Cu:
                if (stage_len > 0u) { lcd_raw_write(stage[stage_idx], stage_len); stage_len = 0u; stage_idx ^= 1u; }
                rle_mode = 0u;               /* 原样段：必须关掉上一段的行程解码 */
                seg_mode = 0u;`,
'command branches');

// 0x0D 分支也要置 seg_mode = 1
must(
`                stage_flush();
                rle_mode = f[5];`,
`                stage_flush();
                seg_mode = 1u;
                rle_mode = f[5];`,
'rle seg_mode');

// 5) 主循环分派
must(
`                    if (rle_mode) {
                        for (i = 0; i < take; i++)
                            rle_feed((uint8_t)rx[pos + (int)i]);
                        pos += (int)take;
                        raw_left -= take;
                        if (raw_left == 0u)
                            stage_flush();
                    } else {`,
`                    if (seg_mode == 2u) {
                        for (i = 0; i < take; i++)
                            pal_feed((uint8_t)rx[pos + (int)i]);
                        pos += (int)take;
                        raw_left -= take;
                    } else if (seg_mode == 3u) {
                        for (i = 0; i < take; i++)
                            idx4_feed((uint8_t)rx[pos + (int)i]);
                        pos += (int)take;
                        raw_left -= take;
                        if (raw_left == 0u)
                            stage_flush();
                    } else if (seg_mode == 1u) {
                        for (i = 0; i < take; i++)
                            rle_feed((uint8_t)rx[pos + (int)i]);
                        pos += (int)take;
                        raw_left -= take;
                        if (raw_left == 0u)
                            stage_flush();
                    } else {`,
'main loop dispatch');

// 6) 断流保护里复位 seg_mode
must(
`                raw_left = 0u;
                stage_len = 0u;
                rle_n = 0u;`,
`                raw_left = 0u;
                stage_len = 0u;
                rle_n = 0u;
                seg_mode = 0u;`,
'stall reset');

if (crlf) s = s.replace(/\n/g, '\r\n');
fs.writeFileSync(P, s, 'utf8');
console.log('main_proto.c: 4-bit indexed colour added (0x0E / 0x0F)');