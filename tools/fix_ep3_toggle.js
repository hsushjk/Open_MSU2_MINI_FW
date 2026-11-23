// 发送节流 + 翻转位交替
const fs = require('fs');
const dir = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/';

// 1) usb.c: usb_write 交替 TOG
let u = fs.readFileSync(dir + 'usb.c', 'utf8');
const start = u.indexOf('int usb_write(const uint8_t *src, int len)');
const tail = u.substring(start);
const end = tail.indexOf('\n}\n') + 3;
const old = tail.substring(0, end);

const neu = [
    'int usb_write(const uint8_t *src, int len)',
    '{',
    '    static uint8_t tog;',
    '    int i;',
    '    if (len > 64)',
    '        len = 64;',
    '    for (i = 0; i < len; i++)',
    '        ep3_buf[i] = src[i];',
    '    *(volatile uint32_t *)0x4002341Cu = (uint32_t)(unsigned long)ep3_buf;',
    '    *(volatile uint16_t *)0x4002342Cu = (uint16_t)len;',
    '    /* 低两位 ACK，bit6 是数据翻转：每一笔必须交替，主机才认 */',
    '    *(volatile uint8_t  *)0x4002342Eu = tog ? 0x40u : 0x00u;',
    '    tog = (uint8_t)(tog ^ 1u);',
    '    return len;',
    '}',
    ''
].join('\n');
u = u.replace(old, neu);
fs.writeFileSync(dir + 'usb.c', u, 'utf8');

// 2) main: 每 400 ms 才发一包
let m = fs.readFileSync(dir + 'main_usbdirect.c', 'utf8');
m = m.replace(
    '        if (usb_write(hello, 6) > 0)\n            ok_cnt++;\n        else\n            busy_cnt++;',
    '        if ((beats % 40u) == 0u) {\n            if (usb_write(hello, 6) > 0)\n                ok_cnt++;\n            else\n                busy_cnt++;\n        }'
);
fs.writeFileSync(dir + 'main_usbdirect.c', m, 'utf8');
console.log('throttle + toggle alternation applied');