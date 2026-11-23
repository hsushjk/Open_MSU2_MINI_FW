/* LCD driver for the 0.96" 160x80 ST7735S panel on MSU2_MINI.
 *
 * Written from the reconstructed firmware (see ../decompiled/lcd.c in the
 * reverse-engineering repository) rather than from the ST7735S datasheet, so
 * that the register-level behaviour matches the vendor image exactly:
 *
 *   - SPI1, master, 8-bit, mode 3, software NSS, fPCLK/2 = 24 MHz
 *   - CS  = PC3      D/C = PA6      RESET = PA0 (J1.3)
 *   - pixel bursts go through DMA1 channel 3, 256 bytes at a time
 *   - the panel's RASET range is offset by 24 rows
 *   - PA9 is toggled together with CS; on this board PA9 has no load, it is a
 *     leftover of the vendor example. Kept so the timing matches.
 */
#include "regs.h"

#define LCD_HOLD   0x200u   /* PA9, unconnected, kept for timing parity */
#define LCD_DC     0x040u   /* PA6 */
#define LCD_CS_GPIO GPIOC
#define LCD_CS     0x008u   /* PC3 */
#define LCD_RST    0x001u   /* PA0, J1.3 */

#define VAR_Y 24

static uint8_t line[256];

/* ------------------------------------------------------------------ */
static void dc_set(int level)
{
    if (level)
        GPIOA->BSHR = LCD_DC;
    else
        GPIOA->BCR = LCD_DC;
}

static void cs_set(int level)
{
    if (level) {
        GPIOC->BSHR = LCD_CS;
        GPIOA->BSHR = LCD_HOLD;
    } else {
        GPIOC->BCR = LCD_CS;
        GPIOA->BCR = LCD_HOLD;
    }
}

/* One byte through SPI1, polled. Returns 0 on timeout, like the original. */
void gpio_mode(GPIO_TypeDef *port, uint32_t pins, uint32_t nibble);

uint8_t spi_byte(uint8_t out)
{
    int guard = 0x4000;

    while ((SPI1->STATR & (1u << 1)) == 0) {
        if (--guard == 0)
            return 0;
    }
    SPI1->DATAR = out;

    guard = 0x4000;
    for (;;) {
        if (SPI1->STATR & (1u << 0))
            return (uint8_t)SPI1->DATAR;
        if (--guard == 0)
            return 0;
    }
}

/* ------------------------------------------------------------------ */
/* 丢掉接收寄存器里残留的字节。
 * 面板只写不读，它的最后一笔传输会把对方移回来的字节留在 RXNE 里；
 * 如果接着读 flash 而不清掉，第一个读到的就是这笔陈货，整个事务错位一个字节。 */
void spi_flush(void)
{
    if (SPI1->STATR & 1u)
        (void)SPI1->DATAR;
}

void lcd_command(uint8_t cmd)
{
    cs_set(0);
    dc_set(0);
    spi_byte(cmd);
    cs_set(1);
}

void lcd_data8(uint8_t value)
{
    cs_set(0);
    dc_set(1);
    spi_byte(value);
    cs_set(1);
}

void lcd_data16(uint16_t value)
{
    cs_set(0);
    dc_set(1);
    spi_byte((uint8_t)(value >> 8));
    spi_byte((uint8_t)value);
    cs_set(1);
}

/* 1 ms 的忙等。
 *
 * 3000 次是早期估的数，实测偏慢约 1.7 倍（设定 4 秒实际 7 秒、设定 10 秒实际 17 秒），
 * 于是待机闪烁、开机停留、断开回待机的计时统统被拉长。这里按实测改成 1490 次（设定 3 秒实际 3.5 秒，再缩 1.18 倍）。
 * 只影响空闲/待机里的“等一会儿”，不影响 USB 收发——那一路是中断 + 环形缓冲。
 */
#define DELAY_MS_LOOPS  1490u

void delay_ms(uint32_t ms)
{
    volatile uint32_t n;
    while (ms--) {
        for (n = 0; n < DELAY_MS_LOOPS; n++)
            __asm__ volatile("");
    }
}

/* ------------------------------------------------------------------ */
static void spi_dma_setup(void)
{
    DMA1_CH3->CFGR = DMA_CFGR_DIR | DMA_CFGR_MINC;
}

static int dma_wait_done(uint32_t len)
{
    uint32_t guard = len * 200u + 100000u;
    while ((DMA1->INTFR & DMA_TCIF3) == 0) {
        if (--guard == 0u) {
            DMA1_CH3->CFGR &= ~DMA_CFGR_EN;
            DMA1->INTFCR = DMA_TCIF3;
            return 0;
        }
    }
    DMA1->INTFCR = DMA_TCIF3;
    return 1;
}

static volatile uint8_t dma_pending;

static void dma_start(const uint8_t *src, uint32_t len)
{
    DMA1_CH3->CFGR &= ~DMA_CFGR_EN;
    DMA1_CH3->MADDR = (uint32_t)src;
    DMA1_CH3->PADDR = (uint32_t)&SPI1->DATAR;
    DMA1_CH3->CNTR = len;
    DMA1_CH3->CFGR |= DMA_CFGR_EN;
    dma_pending = 1u;
}

static void dma_finish(void)
{
    uint32_t guard;

    if (!dma_pending)
        return;
    (void)dma_wait_done(0xFFFFu);
    guard = 200000u;
    while ((SPI1->STATR & (1u << 7)) != 0u) {
        if (--guard == 0u)
            break;
    }
    spi_flush();
    dma_pending = 0u;
}

static void dma_send(const uint8_t *src, uint32_t len)
{
    dma_finish();
    dma_start(src, len);
    dma_finish();
}

/* ------------------------------------------------------------------ */
void lcd_set_window(int x0, int y0, int x1, int y1)
{
    lcd_command(0x2A);
    lcd_data16((uint16_t)x0);
    lcd_data16((uint16_t)x1);

    lcd_command(0x2B);
    lcd_data16((uint16_t)(y0 + VAR_Y));
    lcd_data16((uint16_t)(y1 + VAR_Y));

    lcd_command(0x2C);
}

void lcd_fill(int x, int y, int w, int h, uint16_t colour)
{
    uint32_t pixels = (uint32_t)w * (uint32_t)h;
    uint32_t n, i;
    uint8_t hi = (uint8_t)(colour >> 8);
    uint8_t lo = (uint8_t)colour;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    for (i = 0; i < sizeof(line); i += 2) {
        line[i] = hi;
        line[i + 1] = lo;
    }

    cs_set(0);
    dc_set(1);

    n = pixels >> 7;
    for (i = 0; i < n; i++)
        dma_send(line, sizeof(line));

    n = (pixels * 2u) & 0xFFu;
    if (n)
        dma_send(line, n);

    cs_set(1);
}

/* 把一块 RGB565 像素推给面板。数据按字节给出，每个像素高字节在前，
 * 与面板要求一致；调用方保证传输长度不超过 65535 字节（DMA 计数是 16 位）。 */
void lcd_blit(int x, int y, int w, int h, const uint8_t *bytes_pixels)
{
    uint32_t bytes = (uint32_t)w * (uint32_t)h * 2u;

    if (bytes == 0 || bytes > 0xFFFFu)
        return;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    cs_set(0);
    dc_set(1);
    dma_send(bytes_pixels, bytes);
    cs_set(1);
}

/* 直接把一段字节当像素推给面板：不重设窗口，由之前的命令决定落在哪里。
 * 原厂 lcd_stream 就是"拉低片选 -> DMA -> 拉高片选"，窗口在整帧开始时设一次。 */
void lcd_raw_write(const uint8_t *p, uint32_t n)
{
    if (n == 0 || n > 0xFFFFu)
        return;
    dma_finish();          /* 上一段收尾（通常早就传完了） */
    dma_start(p, n);       /* 这一段的传输与后续接收重叠 */
}

/* 等这一笔 DMA 传完，但保持片选低：资产（NOR 里的图）要边读边推，
 * 总线是屏和 flash 共用的，必须一段一段来。 */
void lcd_stream_wait(void)
{
    dma_finish();
}

void lcd_raw_idle(void)
{
    dma_finish();
    cs_set(1);
}

/* 开始一次整帧流式写：设定窗口后保持片选低、D/C 为数据，
 * 之后每段只需推字节，不再重设窗口。 */
void lcd_stream_begin(int x, int y, int x2, int y2)
{
    /* 协议里的 x2/y2 是排他终点（set_size 传的是宽高累加值），
     * lcd_set_window 要的是闭区间终点，所以各减一。原厂 op 7 也是这么写的：
     * lcd_draw_hline(x0, y0, x1 - 1, y1 - 1)。 */
    dma_finish();
    lcd_set_window(x, y, x2 - 1, y2 - 1);
    dc_set(1);
    cs_set(0);
}

/* PA6 同时是 flash 的 MISO 与面板的 D/C。读 flash 之前必须把它切成输入，
 * 否则 MCU 会一边输出一边读，取回的是自己的电平。原厂固件也是这么切换的。 */
void lcd_dc_as_input(void)
{
    gpio_mode(GPIOA, 0x040u, GPIO_MODE_IN_FLOAT);
}

void lcd_dc_as_output(void)
{
    gpio_mode(GPIOA, 0x040u, GPIO_MODE_OUT_PP_10M);
}

/* ------------------------------------------------------------------ */
void gpio_mode(GPIO_TypeDef *port, uint32_t pins, uint32_t nibble)
{
    uint32_t shift;

    /* 引脚 0..7 在 CFGLR，8..15 在 CFGHR，16..23 在 CFGXR（每脚 4 位）。
       原实现只循环到 15，PC16/PC17（USB D-/D+）从来没被配置过。 */
    for (shift = 0; shift < 24; shift++) {
        if (pins & (1u << shift)) {
            uint32_t reg;
            uint32_t pos;
            if (shift < 8) {
                reg = port->CFGLR;
                pos = shift * 4u;
                reg &= ~(0xFu << pos);
                reg |= (nibble & 0xFu) << pos;
                port->CFGLR = reg;
            } else if (shift < 16) {
                reg = port->CFGHR;
                pos = (shift & 7u) * 4u;
                reg &= ~(0xFu << pos);
                reg |= (nibble & 0xFu) << pos;
                port->CFGHR = reg;
            } else {
                reg = port->CFGXR;
                pos = (shift - 16u) * 4u;
                reg &= ~(0xFu << pos);
                reg |= (nibble & 0xFu) << pos;
                port->CFGXR = reg;
            }
        }
    }
}

void lcd_gpio_init(void)
{
    RCC->APB2PCENR |= RCC_APB2_AFIO | RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_SPI1;
    RCC->AHBPCENR |= RCC_AHB_DMA1;

    gpio_mode(GPIOB, 0x080u, GPIO_MODE_OUT_PP_10M); /* PB7 flash CS, idle high */
    GPIOB->BSHR = 0x080u;

    gpio_mode(GPIOA, LCD_RST, GPIO_MODE_OUT_PP_10M); /* PA0 panel reset, idle high */
    GPIOA->BSHR = LCD_RST;

    gpio_mode(GPIOC, LCD_CS, GPIO_MODE_OUT_PP_10M); /* PC3 panel CS, idle high */
    GPIOC->BSHR = LCD_CS;

    gpio_mode(GPIOA, LCD_DC, GPIO_MODE_OUT_PP_10M); /* PA6 D/C */
    gpio_mode(GPIOA, 0x020u, GPIO_MODE_AF_PP_50M);  /* PA5 SCK  */
    gpio_mode(GPIOA, 0x080u, GPIO_MODE_AF_PP_50M);  /* PA7 MOSI */
    gpio_mode(GPIOA, LCD_HOLD, GPIO_MODE_OUT_PP_10M);
    GPIOA->BSHR = LCD_HOLD;

    SPI1->CTLR1 = SPI_CTLR1_MASTER_8BIT_MODE3;
    SPI1->CTLR2 = 0x03u;
    SPI1->CTLR1 |= SPI_CTLR1_SPE;

    spi_dma_setup();
}

void lcd_init(void)
{
    delay_ms(10);
    GPIOA->BCR = LCD_RST;
    delay_ms(10);
    GPIOA->BSHR = LCD_RST;
    delay_ms(10);

    lcd_command(0x11); /* sleep out */
    delay_ms(10);

    lcd_command(0xB1); /* frame rate control */
    lcd_data8(0x05);
    lcd_data8(0x3C);
    lcd_data8(0x3C);

    lcd_command(0xB2);
    lcd_data8(0x05);
    lcd_data8(0x3C);
    lcd_data8(0x3C);

    lcd_command(0xB3);
    lcd_data8(0x05);
    lcd_data8(0x3C);
    lcd_data8(0x3C);
    lcd_data8(0x05);
    lcd_data8(0x3C);
    lcd_data8(0x3C);

    lcd_command(0xB4); /* display inversion */
    lcd_data8(0x03);

    lcd_command(0xC0); /* power control 1 */
    lcd_data8(0xAB);
    lcd_data8(0x0B);
    lcd_data8(0x04);

    lcd_command(0xC1);
    lcd_data8(0xC5);

    lcd_command(0xC2);
    lcd_data8(0x0D);
    lcd_data8(0x00);

    lcd_command(0xC3);
    lcd_data8(0x8D);
    lcd_data8(0x6A);

    lcd_command(0xC4);
    lcd_data8(0x8D);
    lcd_data8(0xEE);

    lcd_command(0xC5);
    lcd_data8(0x0F);

    lcd_command(0xE0); /* positive gamma */
    lcd_data8(0x07);
    lcd_data8(0x0E);
    lcd_data8(0x08);
    lcd_data8(0x07);
    lcd_data8(0x10);
    lcd_data8(0x07);
    lcd_data8(0x02);
    lcd_data8(0x07);
    lcd_data8(0x09);
    lcd_data8(0x0F);
    lcd_data8(0x25);
    lcd_data8(0x36);
    lcd_data8(0x00);
    lcd_data8(0x08);
    lcd_data8(0x04);
    lcd_data8(0x10);

    lcd_command(0xE1); /* negative gamma */
    lcd_data8(0x0A);
    lcd_data8(0x0D);
    lcd_data8(0x08);
    lcd_data8(0x07);
    lcd_data8(0x0F);
    lcd_data8(0x07);
    lcd_data8(0x02);
    lcd_data8(0x07);
    lcd_data8(0x09);
    lcd_data8(0x0F);
    lcd_data8(0x25);
    lcd_data8(0x35);
    lcd_data8(0x00);
    lcd_data8(0x09);
    lcd_data8(0x04);
    lcd_data8(0x10);

    lcd_command(0xFC);
    lcd_data8(0x80);

    lcd_command(0x3A); /* 16 bit per pixel */
    lcd_data8(0x05);

    lcd_command(0x36); /* MADCTL, landscape (panel mounted rotated) */
    lcd_data8(0x78);

    lcd_command(0x29); /* display on */
    lcd_command(0x2A);
    lcd_data16(0x0000);
    lcd_data16(0x0069);
    lcd_command(0x2B);
    lcd_data16(0x0001);
    lcd_data16(0x00A0);
    lcd_command(0x2C);
}

/* 粗略的短延时，1 个循环约 3 个时钟 */
void delay_cycles(uint32_t n)
{
    volatile uint32_t i;
    for (i = 0; i < n * 8u; i++)
        ;
}
