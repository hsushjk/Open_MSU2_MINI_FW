// 记录每个 SETUP 的 bRequest 与描述符类型
const fs = require('fs');

let s = fs.readFileSync('src/usb.c', 'utf8');
if (!s.includes('usb_req_type')) {
    s = s.replace('uint8_t  usb_req_log[16];',
        'uint8_t  usb_req_log[16];\nuint8_t  usb_req_type[16];');

    const old = 'usb_req_log[usb_req_count++] = setup.bRequest;';
    if (!s.includes(old)) { console.error('log line not found'); process.exit(1); }
    s = s.replace(old,
        'usb_req_type[usb_req_count] = (uint8_t)(setup.wValue >> 8);\n' +
        '    usb_req_log[usb_req_count++] = setup.bRequest;');
    fs.writeFileSync('src/usb.c', s, 'utf8');
}

let h = fs.readFileSync('src/fw.h', 'utf8');
if (!h.includes('usb_req_type')) {
    h = h.replace('extern uint8_t usb_req_log[16];',
        'extern uint8_t usb_req_log[16];\nextern uint8_t usb_req_type[16];');
    fs.writeFileSync('src/fw.h', h, 'utf8');
}
console.log('ok: descriptor type logged alongside bRequest');