/* 外挂 flash 探针 v3
 *
 * 一次上电给出三样信息：JEDEC ID、状态寄存器、全片非 0xFF 字节占比。
 *
 * 显示布局（160 × 80）：
 *   四行位图，每行 8 格，左起 bit7 … bit0，白 = 1，黑 = 0
 *     第 1 行  ID[0]，P25D80 应为 0x85
 *     第 2 行  ID[1]，应为 0x60
 *     第 3 行  ID[2]，应为 0x14
 *     第 4 行  状态寄存器（黄色），bit0 = WIP，空闲时为 0
 *   右上角 6×6 方块：ID 正确为绿，否则洋红
 *   底部横条：全片 1 MB 中非 0xFF 字节的占比，满格 = 全片都有数据
 */
#include "fw.h"

#define FLASH_SIZE 0x100000u
#define SCAN_CHUNK 256u
#define BAR_CELLS  40

static void bit_row(int y, uint8_t value, uint16_t on)
{
    int i;
    for (i = 0; i < 8; i++) {
        int bit = (value >> (7 - i)) & 1;
        lcd_fill(8 + i * 17, y, 14, 10, bit ? on : 0x0000);
    }
}

static int id_ok(const uint8_t id[3])
{
    return id[0] == 0x85u && id[1] == 0x60u && id[2] == 0x14u;
}

int main(void)
{
    static uint8_t buf[SCAN_CHUNK];
    uint8_t  id[3];
    uint8_t  st;
    uint32_t addr;
    uint32_t dirty = 0;
    int      i;
    int      filled;

    RCC->CTLR |= 1u;
    FLASH->ACTLR = (FLASH->ACTLR & ~3u) | 2u;
    RCC->CFGR0 &= 0xF8FFFF0Fu;

    lcd_gpio_init();
    lcd_init();

    lcd_fill(0, 0, 160, 80, 0x0010);

    flash_read_id(id);
    st = flash_read_status();

    bit_row(4,  id[0], 0xFFFF);
    bit_row(20, id[1], 0xFFFF);
    bit_row(36, id[2], 0xFFFF);
    bit_row(52, st,    0xFFE0);

    lcd_fill(146, 2, 6, 6, id_ok(id) ? 0x07E0 : 0xF81F);

    /* 全片扫描，统计非 0xFF 字节。约 0.4 秒。 */
    for (addr = 0; addr < FLASH_SIZE; addr += SCAN_CHUNK) {
        flash_read(addr, buf, (int)SCAN_CHUNK);
        for (i = 0; i < (int)SCAN_CHUNK; i++)
            if (buf[i] != 0xFFu)
                dirty++;
    }

    filled = (int)((dirty * (uint32_t)BAR_CELLS) / FLASH_SIZE);
    if (dirty != 0 && filled == 0)
        filled = 1;
    for (i = 0; i < BAR_CELLS; i++)
        lcd_fill(i * 4, 68, 3, 8, i < filled ? 0xFFE0 : 0x0000);

    for (;;)
        ;
    return 0;
}