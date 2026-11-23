// 每次中断处理结束时把 EP0 的 RX 应答重新置成 ACK，否则控制传输收尾之后
// 的下一包 SETUP 会被 NAK，主机只能复位重来。
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');

if (s.includes('/* 收尾：把 RX 重新置为 ACK')) {
    console.log('already patched');
    process.exit(0);
}

const anchor = '        usb_other_count++;\n        break;\n    }\n}';
const anchorCRLF = '        usb_other_count++;\r\n        break;\r\n    }\r\n}';

let out = null;
if (s.includes(anchorCRLF)) {
    out = s.replace(anchorCRLF,
        '        usb_other_count++;\r\n        break;\r\n    }\r\n\r\n' +
        '    /* 收尾：把 RX 重新置为 ACK，等下一包 SETUP */\r\n' +
        '    UEP0_H = (uint8_t)(UEP0_H & 0x40u);\r\n}');
} else if (s.includes(anchor)) {
    out = s.replace(anchor,
        '        usb_other_count++;\n        break;\n    }\n\n' +
        '    /* 收尾：把 RX 重新置为 ACK，等下一包 SETUP */\n' +
        '    UEP0_H = (uint8_t)(UEP0_H & 0x40u);\n}');
}

if (out === null) { console.error('anchor not found'); process.exit(1); }
fs.writeFileSync(p, out, 'utf8');
console.log('usb.c: EP0 RX re-armed to ACK at end of every service call');