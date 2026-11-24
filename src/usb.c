/* USB 设备栈：按原厂 vec_B8（0x1110）的控制流逐段重写。
 *
 * 与原厂的对应关系：
 *   SETUP  → 拷贝 8 字节 → 按 bmRequestType / bRequest 分派 → 统一收尾装包
 *   IN     → 请求 0x05 写 DEV_ADDR；请求 0x06 续发 64 字节
 *   OUT    → EP0 状态阶段 ACK；EP2 取数据
 *   复位   → DEV_ADDR=0、重配端点、清状态
 *
 * 装包规则（原厂 0x1666..0x1678 那段）：
 *   设备到主机：长度 = min(wLength, 64)，UEP0_CTRL_H = 0x40（绝对写）
 *   无数据请求：长度 = 0，不动 UEP0_CTRL_H
 *   续包：长度 64，UEP0_CTRL_H ^= 0x40
 */
#include "fw.h"
#include "usb_desc.h"

#define USB_BASE 0x40023400u

typedef struct {
    volatile uint8_t  BASE_CTRL;
    volatile uint8_t  UDEV_CTRL;
    volatile uint8_t  INT_EN;
    volatile uint8_t  DEV_ADDR;
    volatile uint8_t  RESERVED0;
    volatile uint8_t  MIS_ST;
    volatile uint8_t  INT_FG;
    volatile uint8_t  INT_ST;
    volatile uint16_t RX_LEN;
    volatile uint8_t  RESERVED1[2];
    volatile uint8_t  UEP4_1_MOD;
    volatile uint8_t  UEP2_3_MOD;
    volatile uint8_t  UEP567_MOD;
    volatile uint8_t  RESERVED2;
    volatile uint32_t UEP0_DMA;
    volatile uint32_t UEP1_DMA;
    volatile uint32_t UEP2_DMA;
    volatile uint32_t UEP3_DMA;
    volatile uint32_t UEP0_CTRL;
    volatile uint32_t UEP1_CTRL;
    volatile uint32_t UEP2_CTRL;
    volatile uint32_t UEP3_CTRL;
    volatile uint32_t UEP4_CTRL;
} usb_regs_t;

#define USB ((usb_regs_t *)USB_BASE)

#define UEP0_LEN (*(volatile uint16_t *)0x40023420u)
#define UEP0_H   (*(volatile uint8_t  *)0x40023422u)
#define UEP2_H   (*(volatile uint8_t  *)0x4002342Au)

#define INT_FG_TRANSFER 0x02u
#define INT_FG_RESET    0x01u
#define INT_FG_SUSPEND  0x04u

#define TOKEN_MASK  0x30u
#define TOKEN_SETUP 0x30u
#define TOKEN_IN    0x20u
#define TOKEN_OUT   0x00u

static uint8_t  ep0_buf[64] __attribute__((aligned(4)));
static uint8_t  ep2_buf[64] __attribute__((aligned(4)));
static uint8_t  ep3_buf[64] __attribute__((aligned(4)));
static uint8_t  ep1_buf[8]  __attribute__((aligned(4)));

#define RX_RING_SIZE 8192u
static uint8_t  rx_ring[RX_RING_SIZE];
static uint16_t rx_head;
static uint16_t rx_tail;
static uint8_t  ep2_nak;      /* 环满时给主机 NAK，等腾出空间 */

static uint8_t  bmRequestType;
static uint8_t  bRequest;
static uint16_t wValue;
static uint16_t wIndex;
static uint16_t wLength;

uint8_t  cfg_selected;
static uint8_t  pending_addr = 0xFFu;
static uint8_t  setup_ok;
static const uint8_t *ep0_ptr;
static uint16_t ep0_left;
static uint8_t  need_greeting;
static uint8_t  greeting_sent;

volatile uint32_t usb_irq_count;
volatile uint32_t usb_reset_count;
volatile uint32_t usb_setup_count;
volatile uint32_t usb_in_count;
volatile uint32_t usb_out_count;
volatile uint16_t usb_last_len;
volatile uint8_t usb_last_b0;
volatile uint8_t usb_last_b1;
volatile uint32_t usb_suspend_count;
volatile uint32_t usb_ep2_drop;
volatile uint32_t usb_rx_stall;
volatile uint32_t usb_other_count;

uint8_t  usb_req_log[16];
uint8_t  usb_req_type[16];
volatile uint32_t usb_req_count;

uint16_t usb_tx_len;
uint8_t  usb_tx_snap[8];
uint8_t  usb_setup_snap[4];
volatile uint8_t ep3_busy;
volatile uint32_t usb_packet_count;
int usb_irq_enable = 1;
volatile uint8_t usb_last_fg;
volatile uint8_t usb_last_st;

void usb_gpio_init(void);

/* ------------------------------------------------------------------ */

static void ep0_length(uint16_t len)
{
    UEP0_LEN = len;
}

/* 设备到主机：首包绝对写 0x40 */
static void ep0_first(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len && i < 64u; i++)
        ep0_buf[i] = data[i];
    usb_tx_len = len;
    for (i = 0; i < 8u; i++)
        usb_tx_snap[i] = (i < len) ? ep0_buf[i] : 0u;
    ep0_length(len);
    UEP0_H = 0x40u;
}

/* 续包：按剩余长度发送，原厂异或翻转 */
static void ep0_next(void)
{
    uint16_t n = ep0_left;
    uint16_t i;
    if (n > 64u)
        n = 64u;
    for (i = 0; i < n; i++)
        ep0_buf[i] = ep0_ptr[i];
    ep0_ptr += n;
    ep0_left = (uint16_t)(ep0_left - n);
    ep0_length(n);
    UEP0_H = (uint8_t)(UEP0_H ^ 0x40u);
}

static void ep0_zlp(void)
{
    ep0_length(0);
}

static void ep0_stall(void)
{
    UEP0_H = 0xcfu;
}

/* ------------------------------------------------------------------ */

static void usb_endpoints_setup(void)
{
    USB->UEP4_1_MOD = 0x4cu;   /* EP1 发 / EP4 发 */
    USB->UEP2_3_MOD = 0x48u;   /* EP2 收 / EP3 发 */
    USB->UEP567_MOD = 0x00u;
    USB->UEP0_DMA = (uint32_t)ep0_buf;
    USB->UEP1_DMA = (uint32_t)ep1_buf;
    USB->UEP2_DMA = (uint32_t)ep2_buf;
    USB->UEP3_DMA = (uint32_t)ep3_buf;
    USB->UEP0_CTRL = 0x00000002u;   /* RX NAK */
    USB->UEP1_CTRL = 0x00020000u;
    USB->UEP2_CTRL = 0x00000000u;   /* RX ACK */
    USB->UEP3_CTRL = 0x00020000u;
    USB->UEP4_CTRL = 0x00020000u;
}

void usb_gpio_init(void)
{
    /* PC16 = USB D- ：浮空输入；PC17 = USB D+ ：复用输入 + 上拉 */
    gpio_mode(GPIOC, 0x00010000u, 0x4u);
    gpio_mode(GPIOC, 0x00020000u, 0x8u);
    GPIOC->BSXR |= (1u << 1);
}

void usb_init(void)
{
    RCC->APB2PCENR |= RCC_APB2_GPIOC | RCC_APB2_AFIO;
    RCC->AHBPCENR |= RCC_AHB_USBFS;

    usb_gpio_init();

    AFIO_CTLR = (AFIO_CTLR & 0xffffff30u) | 0x88u;

    USB->BASE_CTRL = 6u;          /* 复位脉冲 */
    delay_ms(10);
    USB->BASE_CTRL = 0u;
    delay_ms(10);

    usb_endpoints_setup();

    USB->DEV_ADDR = 0;
    USB->BASE_CTRL = 0x29u;
    USB->INT_FG = 0xffu;
    USB->UDEV_CTRL = 0x81u;
    USB->INT_EN = 0x07u;
    NVIC_ISER1 = 0x2000u;
}

/* ------------------------------------------------------------------ */

static void handle_get_descriptor(void)
{
    uint8_t type = (uint8_t)(wValue >> 8);
    uint8_t index = (uint8_t)(wValue & 0xffu);
    uint16_t total = 0;
    const uint8_t *p = usb_desc_get(type, index, &total);
    uint16_t n;

    if (p == 0 || total == 0u) {
        ep0_stall();
        setup_ok = 0;
        return;
    }

    ep0_ptr = p;
    ep0_left = total;

    n = total;
    if (wLength < n)
        n = wLength;
    if (n > 64u)
        n = 64u;

    ep0_first(p, n);
    ep0_ptr = p + n;
    ep0_left = (uint16_t)(total - n);
    setup_ok = 1;
}

static void handle_setup(void)
{
    const volatile uint8_t *sb = (const volatile uint8_t *)ep0_buf;

    bmRequestType = sb[0];
    bRequest = sb[1];
    wValue = (uint16_t)(sb[2] | (sb[3] << 8));
    wIndex = (uint16_t)(sb[4] | (sb[5] << 8));
    wLength = (uint16_t)(sb[6] | (sb[7] << 8));

    usb_setup_snap[0] = sb[0];
    usb_setup_snap[1] = sb[1];
    usb_setup_snap[2] = sb[4];
    usb_setup_snap[3] = sb[6];
    if (usb_req_count < 16u) {
        usb_req_type[usb_req_count] = (uint8_t)(wValue >> 8);
        usb_req_log[usb_req_count++] = sb[1];
    }

    setup_ok = 1;

    if ((bmRequestType & 0x60u) != 0u) {
        /* 类请求：只承诺自己能做的几种 */
        switch (bRequest) {
        case 0x20:                       /* SET_LINE_CODING：数据在 OUT 阶段 */
            ep0_zlp();
            setup_ok = 0;
            return;
        case 0x22:                       /* SET_CONTROL_LINE_STATE */
            /* 上位机每开一次端口都会走到这里：重新武装问候包，
               这样就不必拔插 USB 才能重新认到设备 */
            need_greeting = 1;
            greeting_sent = 0;
            ep0_zlp();
            return;
        case 0x21:                       /* GET_LINE_CODING */
            ep0_buf[0] = 0x00;
            ep0_buf[1] = 0x4B;
            ep0_buf[2] = 0x00;           /* 19200 */
            ep0_buf[3] = 0x00;
            ep0_buf[4] = 0x00;
            ep0_buf[5] = 0x08;           /* 8N1 */
            ep0_buf[6] = 0x00;
            ep0_first(ep0_buf, 7);
            return;
        default:
            ep0_zlp();
            return;
        }
    }

    switch (bRequest) {
    case 0x00:                           /* GET_STATUS */
        ep0_buf[0] = 0;
        ep0_buf[1] = 0;
        ep0_first(ep0_buf, 2);
        return;

    case 0x05:                           /* SET_ADDRESS */
        pending_addr = (uint8_t)(wValue & 0x7fu);
        ep0_zlp();
        return;

    case 0x06:                           /* GET_DESCRIPTOR */
        handle_get_descriptor();
        return;

    case 0x08:                           /* GET_CONFIGURATION */
        ep0_buf[0] = cfg_selected;
        ep0_first(ep0_buf, 1);
        return;

    case 0x09:                           /* SET_CONFIGURATION */
        cfg_selected = (uint8_t)wValue;
        USB->UEP2_3_MOD = 0x48u;
        USB->UEP4_1_MOD = 0x4cu;
        ep0_zlp();
        return;

    case 0x0A:                           /* GET_INTERFACE */
        ep0_buf[0] = 0;
        ep0_first(ep0_buf, 1);
        return;

    case 0x0B:                           /* SET_INTERFACE */
        ep0_zlp();
        return;

    default:
        ep0_stall();
        setup_ok = 0;
        return;
    }
}

/* ------------------------------------------------------------------ */

static void ep0_in_complete(void)
{
    if (bRequest == 0x05u && pending_addr != 0xFFu) {
        USB->DEV_ADDR = (uint8_t)((USB->DEV_ADDR & 0x80u) | pending_addr);
        pending_addr = 0xFFu;
        return;
    }

    if (bRequest == 0x06u && ep0_left > 0u) {
        ep0_next();
        return;
    }

    /* 没有后续数据：只清长度，控制字保持原厂写法不动 */
    UEP0_LEN = 0;
}

void usb_service(void)
{
    uint8_t int_fg, int_st;
    uint16_t len;

    usb_irq_count++;

    int_st = USB->INT_ST;
    len = USB->RX_LEN;
    int_fg = USB->INT_FG;

    usb_last_fg = int_fg;
    usb_last_st = int_st;

    if ((int_fg & INT_FG_TRANSFER) == 0u) {
        if ((int_fg & INT_FG_RESET) != 0u) {
            usb_reset_count++;
            USB->DEV_ADDR = 0;
            pending_addr = 0xFFu;
            cfg_selected = 0;
            setup_ok = 0;
            ep0_ptr = 0;
            ep0_left = 0;
            usb_endpoints_setup();
            USB->INT_EN = 0x07u;
        }
        if ((int_fg & INT_FG_SUSPEND) != 0u) {
            usb_suspend_count++;
        }
        if ((int_fg & (INT_FG_RESET | INT_FG_SUSPEND)) == 0u)
            usb_other_count++;
        USB->INT_FG = int_fg;
        UEP0_H = (uint8_t)(UEP0_H & 0x40u);
        return;
    }

    if ((int_st & TOKEN_MASK) == TOKEN_SETUP) {
        usb_setup_count++;
        /* 复位后第一包 SETUP 之前，端点 RX 必须是 ACK 才能收到 */
        UEP0_H = 0x40u;
        handle_setup();
        USB->INT_FG = INT_FG_TRANSFER;
        return;
    }

    if ((int_st & TOKEN_MASK) == TOKEN_IN) {
        usb_in_count++;
        if ((int_st & 0x07u) == 3u) {
            ep3_busy = 0u;              /* EP3 这一笔已被主机取走 */
        } else if ((int_st & 0x07u) == 0u) {
            ep0_in_complete();
        }
        USB->INT_FG = INT_FG_TRANSFER;
        return;
    }

    usb_out_count++;
    usb_last_len = len;
    usb_last_b0 = ((volatile const uint8_t *)ep2_buf)[0];
    usb_last_b1 = ((volatile const uint8_t *)ep2_buf)[1];
    if ((int_st & 0x3fu) == 0x02u) {
        uint16_t k;
        uint16_t used = (uint16_t)((rx_head - rx_tail) & (RX_RING_SIZE - 1u));
        uint16_t free_bytes = (uint16_t)(RX_RING_SIZE - used);

        if (free_bytes < 64u) {
            /* 理论上到不了这里：上一包拷完已提前置 NAK */
            ep2_nak = 1u;
            usb_rx_stall++;   /* never drop: NAK only, data stays in the host */
            UEP2_H = (uint8_t)((UEP2_H & 0xfcu) | 0x02u);
        } else {
            for (k = 0; k < len && k < 64u; k++) {
                rx_ring[rx_head] = ((volatile const uint8_t *)ep2_buf)[k];
                rx_head = (uint16_t)((rx_head + 1u) & (RX_RING_SIZE - 1u));
            }
            /* 只要这一包（<=64 B）放得下就 ACK，不再"余量不足 2 KB 就提前 NAK"：
             * 提前 NAK 会让主机的 OUT URB 部分完成（usbmon 实测 1280 B 只完成
             * 768 B），Linux 的 cdc_acm 为此要退避几百毫秒。不过余量节流并不是
             * 整帧慢的主因（真凶在主机写路径，见 msu2_core/transport.py 的非阻塞
             * 写）。所以这里只简化策略：这一包真放不下（free < 64，上面已处理）
             * 才 NAK，字节留在主机侧，不丢数据。 */
            UEP2_H = (uint8_t)(UEP2_H & 0xfcu);            /* ACK */
        }
    } else {
        /* EP0 状态阶段：只清长度 */
        UEP0_LEN = 0;
    }
    USB->INT_FG = INT_FG_TRANSFER;
}

__attribute__((interrupt("machine"))) void USBFS_IRQHandler(void)
{
    usb_service();
}

/* ------------------------------------------------------------------ */

int usb_read(uint8_t *dst, int max)
{
    int n = 0;
    while (n < max && rx_tail != rx_head) {
        dst[n++] = rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1u) & (RX_RING_SIZE - 1u));
    }
    if (ep2_nak) {
        uint16_t used = (uint16_t)((rx_head - rx_tail) & (RX_RING_SIZE - 1u));
        uint16_t free_bytes = (uint16_t)(RX_RING_SIZE - used);
        if (free_bytes >= 2048u) {
            ep2_nak = 0u;
            UEP2_H = (uint8_t)(UEP2_H & 0xfcu);              /* ACK */
        }
    }
    return n;
}

int usb_write(const uint8_t *src, int len)
{
    static uint8_t tog;
    int i;
    if (len > 64)
        len = 64;
    if (!len)
        return 0;
    for (i = 0; i < len; i++)
        ep3_buf[i] = src[i];
    *(volatile uint32_t *)0x4002341Cu = (uint32_t)(unsigned long)ep3_buf;
    *(volatile uint16_t *)0x4002342Cu = (uint16_t)len;
    /* ACK 位、bit6 数据翻转，每一笔交替，不能写死 */
    *(volatile uint8_t  *)0x4002342Eu = tog ? 0x40u : 0x00u;
    tog = (uint8_t)(tog ^ 1u);
    return len;
}

void usb_pump(void)
{
}

int usb_take_greeting(uint8_t *dst)
{
    if (!need_greeting || greeting_sent)
        return 0;
    greeting_sent = 1;
    dst[0] = 0x00;
    dst[1] = 'M';
    dst[2] = 'S';
    dst[3] = 'N';
    dst[4] = '0';
    dst[5] = '2';
    return 6;
}