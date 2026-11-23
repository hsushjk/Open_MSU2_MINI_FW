/* CSR 分步测试固件（-Variant csrtest）
 *
 * 启动文件在这个变体里不写 mtvec/mstatus，由 main 在运行时写，
 * 这样屏幕上的颜色就把"写 CSR 之前"和"写 CSR 之后"分开：
 *
 *   红   → 屏幕可用（此时 mtvec 仍为复位值 0，中断关闭）
 *   绿   → 已经写入 mtvec=7 与 mstatus=0x88
 *   蓝/白交替 → 之后持续重绘
 *
 * 如果红色阶段就在闪，问题不在 CSR 写入；如果红色稳定、写完后开始闪，
 * 那就是这两个 CSR 写入或随后的中断引起的。
 */
#include "fw.h"

static void screen(uint16_t colour)
{
    lcd_fill(0, 0, 160, 80, colour);
}

int main(void)
{
    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;

    lcd_gpio_init();
    lcd_init();

    screen(0xF800);            /* 红 */
    delay_ms(3000);

    __asm__ volatile(
        "li t0, 7\n"
        "csrw 0x305, t0\n"
        "li t0, 0x88\n"
        "csrw 0x300, t0\n"
        :
        :
        : "t0");

    screen(0x07E0);            /* 绿 */
    delay_ms(3000);

    for (;;) {
        screen(0x001F);        /* 蓝 */
        delay_ms(300);
        screen(0xFFFF);        /* 白 */
        delay_ms(300);
    }
}