// 每笔发送都重指 DMA、重写长度、把控制字节整体写成 ACK
const fs = require('fs');
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/usb.c';
let s = fs.readFileSync(p, 'utf8');

const start = s.indexOf('int usb_write(const uint8_t *src, int len)');
if (start < 0) {
    console.log('usb_write not found');
    process.exit(1);
}
const tail = s.substring(start);
const end = tail.indexOf('\n}\n') + 3;
const old = tail.substring(0, end);

const neu = [
    'int usb_write(const uint8_t *src, int len)',
    '{',
    '    int i;',
    '    if (len > 64)',
    '        len = 64;',
    '    for (i = 0; i < len; i++)',
    '        ep3_buf[i] = src[i];',
    '    *(volatile uint32_t *)0x4002341Cu = (uint32_t)(unsigned long)ep3_buf;',
    '    *(volatile uint16_t *)0x4002342Cu = (uint16_t)len;',
    '    *(volatile uint8_t  *)0x4002342Eu = 0x00u;',
    '    return len;',
    '}',
    ''
].join('\n');

s = s.replace(old, neu);
fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: usb_write re-points DMA and clears control byte');