// EP0 应答位：每次装包同时把 RX 置为 ACK，否则主机的状态阶段 OUT 会被 NAK
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');

const before = /static void ep0_arm\(uint16_t len\)\s*\{[^}]*\}/;
const m = s.match(before);
if (!m) {
    console.error('ep0_arm not found');
    process.exit(1);
}

const body = [
    'static void ep0_arm(uint16_t len)',
    '{',
    '    /* TOG 翻转，同时把 TX 与 RX 应答都置成 ACK(0)。',
    '     * 只写 TOG 而不管 RX，会让主机的状态阶段 OUT 一直被 NAK。 */',
    '    UEP0_LEN = len;',
    '    UEP0_H = (uint8_t)((UEP0_H ^ 0x40u) & 0x40u);',
    '}'
].join('\n');

s = s.replace(before, body);
fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: ep0_arm now arms RX ACK as well');
console.log(m[0].split('\n').slice(0, 6).join('\n'));