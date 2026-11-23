// 补上 usb_ep2_drop 定义与 ISR 计数
const fs = require('fs');
const base = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/';
let usb = fs.readFileSync(base + 'src/usb.c', 'utf8');
let n = 0;

if (!usb.includes('volatile uint32_t usb_ep2_drop')) {
    usb = usb.replace(
        'volatile uint32_t usb_suspend_count;',
        'volatile uint32_t usb_suspend_count;\nvolatile uint32_t usb_ep2_drop;'
    );
    n++;
}
if (!usb.includes('usb_ep2_drop++;')) {
    usb = usb.replace(
        '            ep2_nak = 1u;',
        '            ep2_nak = 1u;\n            usb_ep2_drop++;'
    );
    n++;
}
fs.writeFileSync(base + 'src/usb.c', usb, 'utf8');
console.log('usb.c patched: ' + n);