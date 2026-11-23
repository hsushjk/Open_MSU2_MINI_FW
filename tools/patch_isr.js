// 重写 USBFS 中断服务：先清全部标志再处理，并统计各类事件，用于定位中断风暴。
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');

if (s.includes('usb_other_count')) {
    console.log('already patched');
    process.exit(0);
}

const startMark = '__attribute__((interrupt("machine"))) void USBFS_IRQHandler(void)';
const i = s.indexOf(startMark);
if (i < 0) { console.error('ISR not found'); process.exit(1); }
const j = s.indexOf('\n}\n', i);
if (j < 0) { console.error('ISR end not found'); process.exit(1); }

const isr = `__attribute__((interrupt("machine"))) void USBFS_IRQHandler(void)
{
    uint8_t  int_fg = USB->INT_FG;
    uint8_t  int_st;
    uint8_t  ep;
    uint16_t len;

    usb_irq_count++;

    /* 先无条件清掉全部中断标志，再处理事件。
     * 原来只在识别出的分支末尾按位清除，任何没识别的位都会让中断线一直有效，
     * CPU 反复重入本函数，主循环被拖慢一个数量级，最后栈溢出死机。 */
    USB->INT_FG = 0xFFu;

    if (int_fg & INT_FG_BUS_RESET) {
        usb_reset_count++;
        USB->DEV_ADDR = 0;
        cfg_selected = 0;
        return;
    }
    if (int_fg & INT_FG_SUSPEND)
        return;

    if ((int_fg & INT_FG_TRANSFER) == 0) {
        usb_other_count++;
        return;
    }

    int_st = USB->INT_ST;
    ep  = (uint8_t)(int_st & 0x3fu);
    len = USB->RX_LEN;

    switch (int_st & INT_ST_MASK_TOKEN) {
    case TOKEN_SETUP:
        usb_setup_count++;
        if (ep == EP0)
            handle_setup();
        break;

    case TOKEN_IN:
        usb_in_count++;
        if (ep == EP0) {
            /* 描述符还有剩余，继续发下一包 */
            if ((setup.bRequest == 0x06) && (setup.bmRequestType & 0x60) == 0) {
                uint16_t total = 0;
                const uint8_t *p = usb_desc_get((uint8_t)(setup.wValue >> 8),
                                                (uint8_t)setup.wValue, &total);
                if (p != 0 && desc_offset < total && desc_offset < setup.wLength) {
                    uint16_t n = total - desc_offset;
                    if (n > setup.wLength - desc_offset)
                        n = setup.wLength - desc_offset;
                    if (n > 64)
                        n = 64;
                    ep0_send(p + desc_offset, n);
                    desc_offset += n;
                    break;
                }
            }
            USB->UEP0_CTRL = (USB->UEP0_CTRL & 0xffff0000u) | UEP_R_ACK;
        } else if (ep == EP3) {
            reply_pending = 0;
        }
        break;

    case TOKEN_OUT:
        usb_out_count++;
        if (ep == EP2) {
            uint16_t k;
            for (k = 0; k < len && k < 64; k++) {
                uint16_t next = (uint16_t)((rx_head + 1) & 0x1ff);
                if (next == rx_tail)
                    break;
                rx_ring[rx_head] = ep2_buf[k];
                rx_head = next;
            }
            USB->UEP2_CTRL = (USB->UEP2_CTRL & 0xffff0000u) | UEP_R_ACK;
        } else if (ep == EP0) {
            USB->UEP0_CTRL = (USB->UEP0_CTRL & 0xffff0000u) | UEP_R_ACK;
        }
        break;

    default:
        usb_other_count++;
        break;
    }
}
`;

s = s.slice(0, i) + isr + s.slice(j + 3);

// 计数器定义
s = s.replace('volatile uint32_t usb_irq_count = 0;',
    `volatile uint32_t usb_irq_count = 0;
volatile uint32_t usb_setup_count = 0;
volatile uint32_t usb_in_count = 0;
volatile uint32_t usb_out_count = 0;
volatile uint32_t usb_other_count = 0;`);

fs.writeFileSync(p, s, 'utf8');
console.log('ISR rewritten: clear-all-first + per-token counters');

// fw.h 增加 extern
const hp = 'src/fw.h';
let h = fs.readFileSync(hp, 'utf8');
if (!h.includes('usb_other_count')) {
    h = h.replace('extern volatile uint32_t usb_irq_count;',
        `extern volatile uint32_t usb_irq_count;
extern volatile uint32_t usb_setup_count;
extern volatile uint32_t usb_in_count;
extern volatile uint32_t usb_out_count;
extern volatile uint32_t usb_other_count;`);
    fs.writeFileSync(hp, h, 'utf8');
    console.log('fw.h updated');
}