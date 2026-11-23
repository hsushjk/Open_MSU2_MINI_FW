// 续包按剩余长度发送；状态阶段不清控制字
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');

const oldNext = [
    '/* 续包：原厂异或翻转 */',
    'static void ep0_next(void)',
    '{',
    '    uint16_t i;',
    '    for (i = 0; i < 64u; i++)',
    '        ep0_buf[i] = ep0_ptr[i];',
    '    ep0_ptr += 64;',
    '    ep0_length(0x40u);',
    '    UEP0_H = (uint8_t)(UEP0_H ^ 0x40u);',
    '}'
].join('\n');

const newNext = [
    '/* 续包：按剩余长度发送，原厂异或翻转 */',
    'static void ep0_next(void)',
    '{',
    '    uint16_t n = ep0_left;',
    '    uint16_t i;',
    '    if (n > 64u)',
    '        n = 64u;',
    '    for (i = 0; i < n; i++)',
    '        ep0_buf[i] = ep0_ptr[i];',
    '    ep0_ptr += n;',
    '    ep0_left = (uint16_t)(ep0_left - n);',
    '    ep0_length(n);',
    '    UEP0_H = (uint8_t)(UEP0_H ^ 0x40u);',
    '}'
].join('\n');

if (!s.includes(oldNext)) {
    console.log('ep0_next shape not found');
    process.exit(1);
}
s = s.replace(oldNext, newNext);

s = s.replace(
    '    if (bRequest == 0x06u && ep0_left > 0u) {\n        ep0_next();\n        ep0_left = (uint16_t)(ep0_left > 64u ? ep0_left - 64u : 0u);\n        return;\n    }',
    '    if (bRequest == 0x06u && ep0_left > 0u) {\n        ep0_next();\n        return;\n    }'
);

s = s.replace(
    '    /* 没有后续数据：留 RX 为 ACK，等状态阶段 */\n    UEP0_LEN = 0;\n    UEP0_H = (uint8_t)(UEP0_H & 0x40u);',
    '    /* 没有后续数据：只清长度，控制字保持原厂写法不动 */\n    UEP0_LEN = 0;'
);

s = s.replace(
    '    } else {\n        /* EP0 状态阶段 */\n        UEP0_LEN = 0;\n        UEP0_H = (uint8_t)(UEP0_H & 0x40u);\n    }',
    '    } else {\n        /* EP0 状态阶段：只清长度 */\n        UEP0_LEN = 0;\n    }'
);

fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: short-packet continuation + status stage fixed');