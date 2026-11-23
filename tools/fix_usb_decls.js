// 修正 usb.c 与 fw.h 的声明不一致
const fs = require('fs');

function edit(path, fn) {
    const s = fs.readFileSync(path, 'utf8');
    const out = fn(s);
    fs.writeFileSync(path, out, 'utf8');
    console.log(path.split(/[\\/]/).pop() + ': ' + (out === s ? 'unchanged' : 'updated'));
}

edit('src/fw.h', s => s.replace(
    'int  usb_take_greeting(void);',
    'int  usb_take_greeting(uint8_t *dst);'
));

edit('src/usb.c', s => {
    s = s.replace(
        'static uint8_t  cfg_selected;',
        'uint8_t  cfg_selected;'
    );
    s = s.replace(
        'uint8_t  usb_tx_len;',
        'uint16_t usb_tx_len;'
    );
    s = s.replace(
        '            ep0_zlp();\n            setup_ok = 0;                /* 等数据阶段结束再回状态 */\n            usb_zlp_pending = 1u;\n            return;',
        '            ep0_zlp();\n            setup_ok = 0;\n            return;'
    );
    s = s.replace(
        '            rx_ring[rx_head] = VOLATILE_READ(ep2_buf)[k];',
        '            rx_ring[rx_head] = ((volatile const uint8_t *)ep2_buf)[k];'
    );
    s = s.replace(
        'uint8_t  usb_setup_snap[4];',
        'uint8_t  usb_setup_snap[4];\nvolatile uint32_t usb_packet_count;\nint usb_irq_enable = 1;'
    );
    s = s.replace(
        '    usb_tx_len = (uint8_t)len;',
        '    usb_tx_len = len;'
    );
    return s;
});