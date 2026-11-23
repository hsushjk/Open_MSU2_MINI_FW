// 提前 NAK：环快满时先把接收应答置 NAK，避免"已 ACK 却丢了"
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/usb.c';
let s = fs.readFileSync(P, 'utf8');

const old = `        if (free_bytes < 64u) {
            ep2_nak = 1u;
            usb_ep2_drop++;
            UEP2_H = (uint8_t)((UEP2_H & 0xfcu) | 0x02u);   /* NAK：环满，主机重试 */
        } else {
            for (k = 0; k < len && k < 64u; k++) {
                rx_ring[rx_head] = ((volatile const uint8_t *)ep2_buf)[k];
                rx_head = (uint16_t)((rx_head + 1u) & (RX_RING_SIZE - 1u));
            }
            UEP2_H = (uint8_t)(UEP2_H & 0xfcu);              /* ACK */
        }`;

const neo = `        if (free_bytes < 64u) {
            /* 理论上到不了这里：上一包拷完已提前置 NAK */
            ep2_nak = 1u;
            usb_ep2_drop++;
            UEP2_H = (uint8_t)((UEP2_H & 0xfcu) | 0x02u);
        } else {
            for (k = 0; k < len && k < 64u; k++) {
                rx_ring[rx_head] = ((volatile const uint8_t *)ep2_buf)[k];
                rx_head = (uint16_t)((rx_head + 1u) & (RX_RING_SIZE - 1u));
            }
            used = (uint16_t)((rx_head - rx_tail) & (RX_RING_SIZE - 1u));
            free_bytes = (uint16_t)(RX_RING_SIZE - used);
            if (free_bytes < 256u) {
                /* 剩余空间不够两包，先 NAK，等主循环腾出空间再放行 */
                ep2_nak = 1u;
                UEP2_H = (uint8_t)((UEP2_H & 0xfcu) | 0x02u);
            } else {
                UEP2_H = (uint8_t)(UEP2_H & 0xfcu);          /* ACK */
            }
        }`;

if (!s.includes(old)) {
    console.error('pattern not found');
    process.exit(1);
}
s = s.replace(old, neo);
fs.writeFileSync(P, s, 'utf8');
console.log('usb.c: EP2 NAK is now set ahead of overflow');