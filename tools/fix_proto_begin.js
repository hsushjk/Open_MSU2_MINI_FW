// op 0x07 = 开始流式：设整帧窗口 + 拉低片选（原厂 lcd_draw_hline 的真正作用）
const fs = require('fs');
const dir = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/';

let c = fs.readFileSync(dir + 'lcd.c', 'utf8');
if (!c.includes('void lcd_stream_begin(')) {
    const add = [
        '/* 开始一次整帧流式写：设定窗口后保持片选低、D/C 为数据，',
        ' * 之后每段只需推字节，不再重设窗口。 */',
        'void lcd_stream_begin(int x, int y, int x2, int y2)',
        '{',
        '    lcd_set_window(x, y, x2, y2);',
        '    dc_set(1);',
        '    cs_set(0);',
        '}',
        '',
        ''
    ].join('\n');
    c = c.replace('/* PA6 同时是 flash 的 MISO 与面板的 D/C。', add + '/* PA6 同时是 flash 的 MISO 与面板的 D/C。');
    fs.writeFileSync(dir + 'lcd.c', c, 'utf8');
}

let h = fs.readFileSync(dir + 'fw.h', 'utf8');
if (!h.includes('lcd_stream_begin')) {
    h = h.replace('void lcd_raw_write(const uint8_t *p, uint32_t n);',
        'void lcd_set_window(int x0, int y0, int x1, int y1);\nvoid lcd_stream_begin(int x, int y, int x2, int y2);\nvoid lcd_raw_write(const uint8_t *p, uint32_t n);');
    fs.writeFileSync(dir + 'fw.h', h, 'utf8');
}

let m = fs.readFileSync(dir + 'main_proto.c', 'utf8');
if (!m.includes('case 0x07')) {
    m = m.replace('        case 0x08: {', '        case 0x07:\n            lcd_stream_begin(x0, y0, x1 - 1, y1 - 1);\n            return;\n        case 0x08: {');
    fs.writeFileSync(dir + 'main_proto.c', m, 'utf8');
}
console.log('proto: stream begin wired to opcode 0x07');