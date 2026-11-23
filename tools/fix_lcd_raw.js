// 用 lcd_raw_write / lcd_raw_idle 取代之前的 lcd_stream_* （后者会重设窗口，语义不对）
const fs = require('fs');
const dir = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/';
let c = fs.readFileSync(dir + 'lcd.c', 'utf8');

const begin = c.indexOf('/* 流式写入：一次性设好窗口并保持片选');
if (begin >= 0) {
    const end = c.indexOf('/* PA6 同时是 flash 的 MISO 与面板的 D/C。');
    c = c.slice(0, begin) + c.slice(end);
}

const add = [
    '/* 直接把一段字节当像素推给面板：不重设窗口，由之前的命令决定落在哪里。',
    ' * 原厂 lcd_stream 就是"拉低片选 -> DMA -> 拉高片选"，窗口在整帧开始时设一次。 */',
    'void lcd_raw_write(const uint8_t *p, uint32_t n)',
    '{',
    '    if (n == 0 || n > 0xFFFFu)',
    '        return;',
    '    cs_set(0);',
    '    dc_set(1);',
    '    dma_send(p, n);',
    '    cs_set(1);',
    '}',
    '',
    'void lcd_raw_idle(void)',
    '{',
    '    cs_set(1);',
    '}',
    '',
    ''
].join('\n');

c = c.replace('/* PA6 同时是 flash 的 MISO 与面板的 D/C。', add + '/* PA6 同时是 flash 的 MISO 与面板的 D/C。');
fs.writeFileSync(dir + 'lcd.c', c, 'utf8');

let h = fs.readFileSync(dir + 'fw.h', 'utf8');
h = h.replace(/void lcd_stream_open\(int x, int y, int w, int h\);\nvoid lcd_stream_chunk\(const uint8_t \*p, uint32_t n\);\nvoid lcd_stream_close\(void\);/,
    'void lcd_raw_write(const uint8_t *p, uint32_t n);\nvoid lcd_raw_idle(void);');
fs.writeFileSync(dir + 'fw.h', h, 'utf8');
console.log('lcd: raw write/reload helpers installed');