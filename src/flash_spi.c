/* P25D80H（1 MB SPI NOR）最小驱动：只为擦除而写。
 *
 * 命令取自原厂实现（decompiled/flash.c）：
 *   0x06 写使能, 0x05 读状态, 0x03 读数据, 0x20 4 KB 扇区擦除, 0xC7 整片擦除
 * 片选是 PB7，与原厂固件一致。
 */

#include "fw.h"

#define FLASH_CS 0x080u /* PB7 */

static void cs_flash(int level)
{
    if (level)
        GPIOB->BSHR = FLASH_CS;
    else {
        spi_flush();
        GPIOB->BCR = FLASH_CS;
    }
}


uint8_t flash_read_status(void)
{
    uint8_t v;
    lcd_dc_as_input();
    cs_flash(0);
    spi_byte(0x05);
    v = spi_byte(0xFF);
    cs_flash(1);
    lcd_dc_as_output();
    return v;
}

void flash_wren(void)
{
    cs_flash(0);
    spi_byte(0x06);
    cs_flash(1);
}

/* 等 WIP 清零；超时返回 0 */
int flash_wait_ready(uint32_t spin)
{
    uint32_t feed = 0u;

    while (spin--) {
        if ((flash_read_status() & 0x01u) == 0)
            return 1;
        /* 整片擦除要 1~2 秒，比 IWDG 的 1.6 秒还长：等待期间必须喂狗 */
        if (++feed >= 4096u) {
            feed = 0u;
            (*(volatile uint32_t *)0x40003000u) = 0xAAAAu;
        }
    }
    return 0;
}

void flash_read(uint32_t addr, uint8_t *dst, int n)
{
    int i;
    lcd_dc_as_input();
    cs_flash(0);
    spi_byte(0x03);
    spi_byte((uint8_t)(addr >> 16));
    spi_byte((uint8_t)(addr >> 8));
    spi_byte((uint8_t)addr);
    for (i = 0; i < n; i++)
        dst[i] = spi_byte(0xFF);
    cs_flash(1);
    lcd_dc_as_output();
}


/* 页编程：每页 256 字节，跨页自动切开；每页都要重新写使能。 */
void flash_program(uint32_t addr, const uint8_t *src, int n)
{
    while (n > 0) {
        int page = 256 - (int)(addr & 0xFFu);
        int i;

        if (page > n)
            page = n;
        flash_wren();
        cs_flash(0);
        spi_byte(0x02);
        spi_byte((uint8_t)(addr >> 16));
        spi_byte((uint8_t)(addr >> 8));
        spi_byte((uint8_t)addr);
        for (i = 0; i < page; i++)
            spi_byte(src[i]);
        cs_flash(1);
        flash_wait_ready(200000u);
        addr += (uint32_t)page;
        src += page;
        n -= page;
    }
}

void flash_erase_4k(uint32_t addr)
{
    flash_wren();
    cs_flash(0);
    spi_byte(0x20);
    spi_byte((uint8_t)(addr >> 16));
    spi_byte((uint8_t)(addr >> 8));
    spi_byte((uint8_t)addr);
    cs_flash(1);
    flash_wait_ready(4000000u);
}

/* 整片擦除：发 0xC7 后必须等到内部完成，否则紧接着的操作会被忽略。
 * 1 MB 全片擦除典型 1~2 秒，flash_wait_ready 里边等边喂看门狗。
 * 有些批次的片子只认 0x60，抽查中段不干净就再补一次。 */
static int flash_chip_erase_once(uint8_t cmd)
{
    flash_wren();
    cs_flash(0);
    spi_byte(cmd);
    cs_flash(1);
    return flash_wait_ready(3000000u);
}

int flash_erase_chip_now(void)
{
    flash_chip_erase_once(0xC7u);
    if (flash_is_blank(0x080000u, 16) && flash_is_blank(0x0C0000u, 16))
        return 1;
    flash_chip_erase_once(0x60u);
    return flash_is_blank(0x080000u, 16) && flash_is_blank(0x0C0000u, 16);
}

int flash_is_blank(uint32_t addr, int n)
{
    uint8_t buf[16];
    int i;
    if (n > (int)sizeof(buf))
        n = (int)sizeof(buf);
    flash_read(addr, buf, n);
    for (i = 0; i < n; i++)
        if (buf[i] != 0xFFu)
            return 0;
    return 1;
}
void flash_read_id(uint8_t id[3])
{
    lcd_dc_as_input();
    cs_flash(0);
    spi_byte(0x9F);
    id[0] = spi_byte(0xFF);
    id[1] = spi_byte(0xFF);
    id[2] = spi_byte(0xFF);
    cs_flash(1);
    lcd_dc_as_output();
}