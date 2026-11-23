/* Standalone firmware for MSU2_MINI: draws colour bars on the ST7735S panel.
 *
 * Every register access here corresponds to a sequence observed in the vendor
 * image; nothing is taken from the datasheet alone. See ../decompiled/ for the
 * reconstructed original.
 */
#include "regs.h"

void lcd_gpio_init(void);
void lcd_init(void);
void lcd_fill(int x, int y, int w, int h, uint16_t colour);

static const uint16_t palette[8] = {
    0xFFFF, /* white   */
    0xFFE0, /* yellow  */
    0x07FF, /* cyan    */
    0x07E0, /* green   */
    0xF81F, /* magenta */
    0xF800, /* red     */
    0x001F, /* blue    */
    0x0000, /* black   */
};

int main(void)
{
    int i;

    /* 48 MHz HSI, no PLL, two flash wait states: same as the vendor SystemInit */
    RCC->CTLR |= 1u;
    FLASH->ACTLR = (FLASH->ACTLR & ~3u) | 2u;
    RCC->CFGR0 &= 0xF8FFFF0Fu;

    board_clock_init();
    lcd_gpio_init();
    lcd_init();

    for (i = 0; i < 8; i++)
        lcd_fill(i * 20, 0, 20, 80, palette[i]);

    for (;;)
        ;

    return 0;
}