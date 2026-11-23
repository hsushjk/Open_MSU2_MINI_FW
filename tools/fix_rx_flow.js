// 接收环流控：环满时给主机 NAK，腾出空间后再放行
const fs = require('fs');
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/usb.c';
let s = fs.readFileSync(p, 'utf8');
let n = 0;

function rep(from, to) {
    if (!s.includes(from)) { console.log('MISS: ' + from.slice(0, 40)); return; }
    s = s.replace(from, to); n++;
}

rep('#define RX_RING_SIZE 4096u', '#define RX_RING_SIZE 8192u');
rep('static uint16_t rx_tail;', 'static uint16_t rx_tail;\nstatic uint8_t  ep2_nak;      /* 环满时给主机 NAK，等腾出空间 */');

const oldOut = `    usb_out_count++;
    if ((int_st & 0x3fu) == 0x02u) {
        uint16_t k;
        for (k = 0; k < len && k < 64u; k++) {
            uint16_t next = (uint16_t)((rx_head + 1u) & (RX_RING_SIZE - 1u));
            if (next == rx_tail)
                break;
            rx_ring[rx_head] = ((volatile const uint8_t *)ep2_buf)[k];
            rx_head = next;
        }
        USB->UEP2_CTRL = (USB->UEP2_CTRL & 0xffff0000u) | 0u;
    } else {`;

const newOut = `    usb_out_count++;
    if ((int_st & 0x3fu) == 0x02u) {
        uint16_t k;
        uint16_t free_bytes = (uint16_t)((rx_tail + RX_RING_SIZE - rx_head) & (RX_RING_SIZE - 1u));

        if (free_bytes < 64u) {
            ep2_nak = 1u;
            USB->UEP2_CTRL = (USB->UEP2_CTRL & 0xffff0000u) | 2u;
        } else {
            for (k = 0; k < len && k < 64u; k++) {
                rx_ring[rx_head] = ((volatile const uint8_t *)ep2_buf)[k];
                rx_head = (uint16_t)((rx_head + 1u) & (RX_RING_SIZE - 1u));
            }
            USB->UEP2_CTRL = (USB->UEP2_CTRL & 0xffff0000u) | 0u;
        }
    } else {`;

rep(oldOut, newOut);

const oldRead = `int usb_read(uint8_t *dst, int max)
{
    int n = 0;
    while (n < max && rx_tail != rx_head) {
        dst[n++] = rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1u) & (RX_RING_SIZE - 1u));
    }
    return n;
}`;

const newRead = `int usb_read(uint8_t *dst, int max)
{
    int n = 0;
    while (n < max && rx_tail != rx_head) {
        dst[n++] = rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1u) & (RX_RING_SIZE - 1u));
    }
    if (ep2_nak) {
        uint16_t free_bytes = (uint16_t)((rx_tail + RX_RING_SIZE - rx_head) & (RX_RING_SIZE - 1u));
        if (free_bytes >= 2048u) {
            ep2_nak = 0u;
            USB->UEP2_CTRL = (USB->UEP2_CTRL & 0xffff0000u) | 0u;
        }
    }
    return n;
}`;

rep(oldRead, newRead);

fs.writeFileSync(p, s);
console.log('rx flow control: ' + n + ' replacements');