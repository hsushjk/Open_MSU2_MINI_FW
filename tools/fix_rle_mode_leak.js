// 0x0C（原样段）必须清掉 rle_mode，否则上一段的压缩标志会延续到后续原样段
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
let s = fs.readFileSync(P, 'utf8');

const old = `            case 0x0Cu:
                if (stage_len > 0u) { lcd_raw_write(stage[stage_idx], stage_len); stage_len = 0u; stage_idx ^= 1u; }
                raw_left = (uint32_t)((f[3] << 8) | f[4]);
                return;`;

const neo = `            case 0x0Cu:
                if (stage_len > 0u) { lcd_raw_write(stage[stage_idx], stage_len); stage_len = 0u; stage_idx ^= 1u; }
                rle_mode = 0u;               /* 原样段：必须关掉上一段的行程解码 */
                rle_n = 0u;
                raw_left = (uint32_t)((f[3] << 8) | f[4]);
                return;`;

if (!s.includes(old)) {
    console.error('pattern not found');
    process.exit(1);
}
s = s.replace(old, neo);
fs.writeFileSync(P, s, 'utf8');
console.log('main_proto.c: 0x0C now clears rle_mode');