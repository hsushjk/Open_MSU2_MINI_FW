// EP2 的接收应答位在控制字高字节（0x4002342A）的 bit1:0：0=ACK 2=NAK 3=STALL
const fs = require('fs');
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/usb.c';
let s = fs.readFileSync(p, 'utf8');
let n = 0;
function rep(a, b) { if (!s.includes(a)) { console.log('MISS: ' + a.slice(0, 60)); return; } s = s.replace(a, b); n++; }

rep('#define UEP0_H (*(volatile uint8_t *)0x40023422u)',
    '#define UEP0_H (*(volatile uint8_t *)0x40023422u)\n#define UEP2_H (*(volatile uint8_t *)0x4002342Au)');

rep('            USB->UEP2_CTRL = (USB->UEP2_CTRL & 0xffff0000u) | 2u;',
    '            UEP2_H = (uint8_t)((UEP2_H & 0xfcu) | 0x02u);   /* NAK：环满，主机重试 */');

rep('            USB->UEP2_CTRL = (USB->UEP2_CTRL & 0xffff0000u) | 0u;',
    '            UEP2_H = (uint8_t)(UEP2_H & 0xfcu);              /* ACK */');

rep('            USB->UEP2_CTRL = (USB->UEP2_CTRL & 0xffff0000u) | 0u;',
    '            UEP2_H = (uint8_t)(UEP2_H & 0xfcu);              /* ACK */');

fs.writeFileSync(p, s);
console.log('ep2 ack/nak: ' + n + ' replacements');