/* USB 上机验证固件（-Variant usb）
 *
 * 背景说明：这块屏上"整屏深灰"和"全黑"肉眼几乎分不出来，所以本固件所有
 * 状态都用固定位置的大色块表达，不再依赖底色。
 *
 * 屏幕布局（每个方块 20×20，y=0，x = 0/24/48/72）：
 *   红     = lcd_init 与首次刷屏完成
 *   黄     = usb_init() 已返回
 *   绿     = 进过一次 USB 中断（usb_irq_count > 0）
 *   青     = 有过一次传输完成事件
 *
 * 另有：底部 4 格进度条（每格 40 px）= 累计收到的字节数，签完握手后走。
 */
#include "fw.h"

#define SCREEN_W 160
#define SCREEN_H 80

static uint8_t buf[64];
static uint32_t total;

static const uint16_t C_RED = 0xF800;
static const uint16_t C_YEL = 0xFFE0;
static const uint16_t C_GRN = 0x07E0;
static const uint16_t C_CYA = 0x07FF;

static void block(int x, uint16_t colour)
{
    lcd_fill(x, 0, 20, 20, colour);
}

static int is_msncn(const uint8_t *p)
{
    return p[0] == 0x00 && p[1] == 0x4D && p[2] == 0x53 &&
           p[3] == 0x4E && p[4] == 0x43 && p[5] == 0x4E;
}

static void send_hello(void)
{
    static const uint8_t hello[6] = { 0x00, 0x4D, 0x53, 0x4E, 0x30, 0x31 };
    usb_write(hello, 6);
}

int main(void)
{
    int n;
    uint32_t last_irq = 0xFFFFFFFFu;
    uint32_t last_pkt = 0xFFFFFFFFu;
    uint32_t last_cells = 0xFFFFFFFFu;

    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;
    board_clock_init();
    lcd_gpio_init();
    lcd_init();

    lcd_fill(0, 0, SCREEN_W, SCREEN_H, 0x0000); /* 纯黑，只为把屏幕清干净 */
    block(0, C_RED);

    usb_init();
    block(24, C_YEL);

    for (;;) {
        if (usb_irq_count != last_irq) {
            last_irq = usb_irq_count;
            if (last_irq)
                block(48, C_GRN);
        }
        if (usb_packet_count != last_pkt) {
            last_pkt = usb_packet_count;
            if (last_pkt)
                block(72, C_CYA);
        }

        n = usb_read(buf, (int)sizeof buf);
        if (n > 0) {
            if (n >= 6 && is_msncn(buf))
                lcd_fill(0, 24, 40, 12, C_GRN); /* 握手回执收到 */
            total += (uint32_t)n;
        }

        {
            uint32_t cells = total / 64u;
            if (cells > 4u)
                cells = 4u;
            if (cells != last_cells) {
                last_cells = cells;
                lcd_fill(0, SCREEN_H - 5, 160, 5, 0x0000);
                if (cells)
                    lcd_fill(0, SCREEN_H - 5, (int)(cells * 40u), 5, 0x001F);
            }
        }

        /* 打招呼：收到主机打开端口的事件后，每 400 ms 发一次 */
        if (usb_take_greeting()) {
            send_hello();
            delay_ms(400);
        }
    }
}