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
    while (spin--) {
        if ((flash_read_status() & 0x01u) == 0)
            return 1;
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

void flash_erase_chip(void)
{
    flash_wren();
    cs_flash(0);
    spi_byte(0xC7);
    cs_flash(1);
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