// 记录前 8 个 SETUP 的 bRequest，供屏幕显示
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');

if (s.includes('usb_req_log')) {
    console.log('already patched');
    process.exit(0);
}

s = s.replace(
    'static uint8_t  rx_ring[512];',
    'static uint8_t  rx_ring[512];\n\n' +
    '/* 前 8 个 SETUP 的 bRequest，调试用 */\n' +
    'uint8_t usb_req_log[8];\n' +
    'volatile uint32_t usb_req_count;'
);

s = s.replace(
    '    switch (setup.bmRequestType & 0x60) {',
    '    if (usb_req_count < 8u)\n' +
    '        usb_req_log[usb_req_count++] = setup.bRequest;\n\n' +
    '    switch (setup.bmRequestType & 0x60) {'
);

fs.writeFileSync(p, s, 'utf8');

const h = 'src/fw.h';
let t = fs.readFileSync(h, 'utf8');
if (!t.includes('usb_req_log')) {
    t = t.replace(
        'extern volatile uint32_t usb_packet_count;',
        'extern volatile uint32_t usb_packet_count;\n' +
        'extern uint8_t usb_req_log[8];\n' +
        'extern volatile uint32_t usb_req_count;'
    );
    fs.writeFileSync(h, t, 'utf8');
}
console.log('usb.c: SETUP request log added');