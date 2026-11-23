// EP3 发送改为"完成中断后才允许下一笔"
const fs = require('fs');
let s = fs.readFileSync('src/usb.c', 'utf8');

// 1. 新增忙标志
if (!s.includes('volatile uint8_t ep3_busy;')) {
    s = s.replace(
        'uint8_t  usb_setup_snap[4];',
        'uint8_t  usb_setup_snap[4];\nvolatile uint8_t ep3_busy;'
    );
}

// 2. usb_write：忙则拒绝，成功则置忙
const start = s.indexOf('int usb_write(const uint8_t *src, int len)');
const tail = s.substring(start);
const endIdx = tail.indexOf('\n}\n') + 3;
const old = tail.substring(0, endIdx);

const neu = [
    'int usb_write(const uint8_t *src, int len)',
    '{',
    '    int i;',
    '    if (len > 64)',
    '        len = 64;',
    '    if (ep3_busy)',
    '        return 0;',
    '    for (i = 0; i < len; i++)',
    '        ep3_buf[i] = src[i];',
    '    *(volatile uint16_t *)0x4002342Cu = (uint16_t)len;',
    '    *(volatile uint8_t  *)0x4002342Eu &= 0xFCu;',
    '    ep3_busy = 1u;',
    '    return len;',
    '}',
    ''
].join('\n');
s = s.replace(old, neu);

// 3. 服务程序里，EP3 完成事件清忙标志
s = s.replace(
    '    usb_in_count++;\n    ep0_in_complete();',
    '    usb_in_count++;\n    if ((int_st & 0x3fu) == 3u)\n        ep3_busy = 0u;\n    ep0_in_complete();'
);

fs.writeFileSync('src/usb.c', s, 'utf8');

// 4. fw.h 声明
let h = fs.readFileSync('src/fw.h', 'utf8');
if (!h.includes('extern volatile uint8_t ep3_busy;')) {
    h = h.replace('extern uint8_t usb_tx_snap[8];', 'extern uint8_t usb_tx_snap[8];\nextern volatile uint8_t ep3_busy;');
    fs.writeFileSync('src/fw.h', h, 'utf8');
}
console.log('usb.c: ep3_busy handshake added');