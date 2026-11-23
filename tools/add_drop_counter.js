// 记录 EP2 丢包次数：ISR 里环满无法拷贝时计数，并在 op9 状态包里回传
const fs = require('fs');
const base = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/';
let usb = fs.readFileSync(base + 'src/usb.c', 'utf8');
let proto = fs.readFileSync(base + 'src/main_proto.c', 'utf8');
let fw = fs.readFileSync(base + 'src/fw.h', 'utf8');
let n = 0;

if (!usb.includes('usb_ep2_drop')) {
    // 定义计数器
    usb = usb.replace(
        'volatile uint32_t usb_reset_count = 0;',
        'volatile uint32_t usb_reset_count = 0;\nvolatile uint32_t usb_ep2_drop = 0;'
    );
    // 环满分支计数
    const marker = 'free < (uint32_t)len) {';
    const idx = usb.indexOf(marker);
    if (idx < 0) { console.log('MISS drop branch'); }
    else {
        const brace = usb.indexOf('{', idx) + 1;
        usb = usb.slice(0, brace) + '\n            usb_ep2_drop++;' + usb.slice(brace);
        n++;
    }
}

if (!fw.includes('usb_ep2_drop')) {
    fw = fw.replace('extern volatile uint32_t usb_reset_count;',
        'extern volatile uint32_t usb_reset_count;\nextern volatile uint32_t usb_ep2_drop;');
    n++;
}

// op9 状态包带上丢包数
if (!proto.includes('usb_ep2_drop')) {
    proto = proto.replace('                    st[3] = (uint8_t)(last_frame_ms & 0xFFu);',
        '                    st[3] = (uint8_t)(usb_ep2_drop > 255u ? 255u : usb_ep2_drop);');
    n++;
}

fs.writeFileSync(base + 'src/usb.c', usb, 'utf8');
fs.writeFileSync(base + 'src/fw.h', fw, 'utf8');
fs.writeFileSync(base + 'src/main_proto.c', proto, 'utf8');
console.log('drop counter patched: ' + n + ' edits');