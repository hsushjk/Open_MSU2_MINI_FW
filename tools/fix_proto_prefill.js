// set_fill 必须先把整段 256 字节缓冲填成该颜色，否则只发差异块时未触碰的块保留旧数据 -> 噪点
const fs = require('fs');
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
let s = fs.readFileSync(p, 'utf8');

// 在 seg 声明后加一个填充函数
s = s.replace(
    'static int seg_hi;                 /* 已收到的最大的块号 */',
    'static int seg_hi;                 /* 已收到的最大的块号 */\n\n' +
    'static void seg_prefill(uint16_t colour)\n' +
    '{\n' +
    '    int i;\n' +
    '    for (i = 0; i < 256; i += 2) {\n' +
    '        seg[i] = (uint8_t)(colour >> 8);\n' +
    '        seg[i + 1] = (uint8_t)colour;\n' +
    '    }\n' +
    '    seg_hi = -1;\n' +
    '}'
);

// set_fill 分支：填色 + 把整段缓冲预填
s = s.replace(
    '        case 0x04:\n            fill_colour = a;\n            break;',
    '        case 0x04:\n' +
    '            fill_colour = a;\n' +
    '            if (streaming)\n' +
    '                seg_prefill(fill_colour);\n' +
    '            break;'
);

fs.writeFileSync(p, s, 'utf8');
console.log('proto: segment prefill on set_fill');