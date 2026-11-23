// 按原厂 usb_ep_send 的写法发 EP3：写长度后只清高字节低两位，不动 bit6
const fs = require('fs');
let s = fs.readFileSync('src/usb.c', 'utf8');

const oldFn = s.substring(s.indexOf('int usb_write(const uint8_t *src, int len)'));
const endIdx = oldFn.indexOf('\n}\n') + 3;
const old = oldFn.substring(0, endIdx);

const neu = [
    'int usb_write(const uint8_t *src, int len)',
    '{',
    '    int i;',
    '    if (len > 64)',
    '        len = 64;',
    '    for (i = 0; i < len; i++)',
    '        ep3_buf[i] = src[i];',
    '    *(volatile uint16_t *)0x4002342Cu = (uint16_t)len;   /* 长度 */',
    '    *(volatile uint8_t  *)0x4002342Eu &= 0xFCu;           /* TX 应答位置 ACK，不碰 bit6 */',
    '    return len;',
    '}',
    ''
].join('\n');

if (!old.includes('0x4002342Cu')) {
    console.log('usb_write not found as expected');
    process.exit(1);
}
s = s.replace(old, neu);
fs.writeFileSync('src/usb.c', s, 'utf8');
console.log('usb.c: usb_write follows vendor (clear low bits only)');