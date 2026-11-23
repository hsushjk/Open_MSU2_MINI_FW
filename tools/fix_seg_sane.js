// 全局保护：任何段长度超过 2 KB 或为 0，一律当作失步，立刻回到帧解析
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
let s = fs.readFileSync(P, 'utf8');
const crlf = s.includes('\r\n');
s = s.replace(/\r\n/g, '\n');

if (s.includes('SEG_SANE(')) { console.log('already patched'); process.exit(0); }

// 在 seg_mode 声明后面加宏
s = s.replace(
`static uint8_t seg_mode;`,
`#define SEG_SANE(n)  ((n) > 0u && (n) <= 2048u)
static uint8_t seg_mode;`);

// 0x0C / 0x0D / 0x0E / 0x10 四处长度赋值后统一校验
let count = 0;
s = s.replace(/raw_left = \(\(uint32_t\)f\[3\] << 8\) \| f\[4\];/g, (m) => {
    count++;
    return m + `\n                if (!SEG_SANE(raw_left)) { raw_left = 0u; seg_mode = 0u; }`;
});
// 0x0D 分支写法略有不同
s = s.replace(/raw_left = \(\(uint32_t\)f\[3\] << 8\) \| f\[4\];\s*\n(\s*)rle_n = 0u;/g,
    (m, sp) => m);

if (crlf) s = s.replace(/\n/g, '\r\n');
fs.writeFileSync(P, s, 'utf8');
console.log('segments guarded:', count);