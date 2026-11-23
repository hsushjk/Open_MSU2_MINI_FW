// 8 位索引色（seg_mode 4）：固定 256 色调色板（6×6×6 色立方），一字节一像素
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
let s = fs.readFileSync(P, 'utf8');
const crlf = s.includes('\r\n');
s = s.replace(/\r\n/g, '\n');

if (s.includes('idx8_feed')) { console.log('already patched'); process.exit(0); }

// 1. 声明：调色板表 + 第八位展开函数
s = s.replace(
`static uint8_t seg_mode;                 /* 0=原样 1=行程 2=调色板 3=4 位索引 */`,
`static uint8_t seg_mode;                 /* 0=原样 1=行程 2=调色板 3=4 位索引 4=8 位索引 */`);

// 2. 在 idx4_feed 之后插入 256 色表与 idx8_feed
const anchor = 'static void idx4_feed(uint8_t b)';
const i = s.indexOf(anchor);
if (i < 0) { console.error('idx4_feed not found'); process.exit(1); }
let depth = 0, j = s.indexOf('{', i);
let k = j;
for (; k < s.length; k++) {
    if (s[k] === '{') depth++;
    else if (s[k] === '}') { depth--; if (depth === 0) { k++; break; } }
}
const insert = `

/* 固定 256 色调色板：R/G/B 各 6 级（0,51,102,153,204,255）的色立方。
 * 顺序固定，主机与固件都按同一个公式生成，所以不需要传输调色板，
 * 也就不会有"每帧调色板漂移导致的整屏闪烁"。 */
static uint16_t pal256[256];
static uint8_t pal256_ready;

static void pal256_init(void)
{
    uint16_t i;
    for (i = 0u; i < 216u; i++) {
        uint16_t r = (uint16_t)(i / 36u);
        uint16_t g = (uint16_t)((i / 6u) % 6u);
        uint16_t b = (uint16_t)(i % 6u);
        uint16_t rv = (uint16_t)(r * 51u);
        uint16_t gv = (uint16_t)(g * 51u);
        uint16_t bv = (uint16_t)(b * 51u);
        pal256[i] = (uint16_t)(((rv >> 3) << 11) | ((gv >> 2) << 5) | (bv >> 3));
    }
    for (i = 216u; i < 256u; i++) {
        pal256[i] = 0u;                       /* 备用位，全黑 */
    }
    pal256_ready = 1u;
}

static void idx8_feed(uint8_t b)
{
    if (!pal256_ready) pal256_init();
    if (stage_len + 2u > sizeof(stage[0])) stage_flush();
    stage[stage_idx][stage_len++] = (uint8_t)(pal256[b] >> 8);
    stage[stage_idx][stage_len++] = (uint8_t)(pal256[b] & 0xFFu);
}`;
s = s.slice(0, k) + insert + s.slice(k);

// 3. 新命令 0x10：8 位索引段
s = s.replace(
`            case 0x0Eu:                          /* 4 位索引段：一字节两个像素 */`,
`            case 0x10u:                          /* 8 位索引段：一字节一个像素 */
                stage_flush();
                raw_left = ((uint32_t)f[3] << 8) | f[4];
                seg_mode = 4u;
                break;
            case 0x0Eu:                          /* 4 位索引段：一字节两个像素 */`);

// 4. 主循环分派
s = s.replace(
`                    } else if (seg_mode == 1u) {`,
`                    } else if (seg_mode == 4u) {
                        for (i = 0; i < raw_left; i++) {
                            idx8_feed((uint8_t)rx[pos + (int)i]);
                        }
                        raw_left = 0u;
                        stage_flush();
                    } else if (seg_mode == 1u) {`);

if (crlf) s = s.replace(/\n/g, '\r\n');
fs.writeFileSync(P, s, 'utf8');
console.log('firmware: 8-bit indexed mode (seg_mode 4, cmd 0x10, fixed 6x6x6 palette)');