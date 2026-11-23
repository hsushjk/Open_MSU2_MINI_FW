// EP3 发送：16 位长度 + 高字节翻转位
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');

const oldFn = [
    'int usb_write(const uint8_t *src, int len)',
    '{',
    '    int i;',
    '    if (len > 64)',
    '        len = 64;',
    '    for (i = 0; i < len; i++)',
    '        ep3_buf[i] = src[i];',
    '    USB->UEP3_CTRL = (uint32_t)(((uint32_t)(int8_t)(len - 64) & 0xffffu) << 16) | (uint32_t)len;',
    '    return len;',
    '}'
].join('\n');

const newFn = [
    'int usb_write(const uint8_t *src, int len)',
    '{',
    '    int i;',
    '    if (len > 64)',
    '        len = 64;',
    '    for (i = 0; i < len; i++)',
    '        ep3_buf[i] = src[i];',
    '    *(volatile uint16_t *)0x4002342Cu = (uint16_t)len;',
    '    *(volatile uint8_t  *)0x4002342Eu ^= 0x40u;',
    '    return len;',
    '}'
].join('\n');

if (!s.includes(oldFn)) {
    console.log('usb_write shape not found');
    process.exit(1);
}
s = s.replace(oldFn, newFn);
fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: usb_write uses 16-bit length + toggle');