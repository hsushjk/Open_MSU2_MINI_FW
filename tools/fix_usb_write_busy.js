// EP3 发送加忙判断：高字节 bit6 置位表示上一笔还没被取走
const fs = require('fs');
let s = fs.readFileSync('src/usb.c', 'utf8');

const oldFn = [
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

const newFn = [
    'int usb_write(const uint8_t *src, int len)',
    '{',
    '    int i;',
    '    if (len > 64)',
    '        len = 64;',
    '    /* 高字节 bit6 置位 = 上一笔还没被主机取走，先不覆盖 */',
    '    if ((*(volatile uint8_t *)0x4002342Eu & 0x40u) != 0u)',
    '        return 0;',
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
fs.writeFileSync('src/usb.c', s, 'utf8');
console.log('usb.c: usb_write checks busy bit');