// 把 USBFS_IRQHandler 的函数体抽成可轮询的 usb_service()，并保留中断入口
const fs = require('fs');

const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');
const lines = s.split(/\r?\n/);

let sig = -1;
for (let i = 0; i < lines.length; i++) {
    if (lines[i].includes('USBFS_IRQHandler(void)')) { sig = i; break; }
}
if (sig < 0) { console.log('signature not found'); process.exit(1); }
if (lines[sig].includes('usb_service')) { console.log('already refactored'); process.exit(0); }

// 找函数体的起止：签名行的下一个 '{' 到其后第一个以 '}' 开头的行
let open = sig;
while (open < lines.length && lines[open].trim() !== '{') open++;
let close = open + 1;
while (close < lines.length && lines[close] !== '}') close++;
if (close >= lines.length) { console.log('body end not found'); process.exit(1); }

lines[sig] = 'void usb_service(void)';
s = lines.join('\n');

s += '\n/* 中断入口：只是转调 usb_service() */\n';
s += 'void __attribute__((interrupt("machine"))) USBFS_IRQHandler(void)\n{\n';
s += '    usb_service();\n}\n';

fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: usb_service extracted (body lines ' + (open + 1) + '-' + (close + 1) + ')');

// fw.h 里补声明
const h = 'src/fw.h';
let t = fs.readFileSync(h, 'utf8');
if (!t.includes('void usb_service(void);')) {
    t = t.replace('extern volatile uint32_t usb_irq_count;',
        'extern volatile uint32_t usb_irq_count;\nvoid usb_service(void);');
    fs.writeFileSync(h, t, 'utf8');
    console.log('fw.h: usb_service declared');
} else {
    console.log('fw.h: already declared');
}