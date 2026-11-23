/* 外挂 flash 读取验证固件
 *
 * 把整片 1 MB 按 82 字节一格映射成 160×80 个格子，每个格子抽查 4 个字节：
 * 全为 0xFF 的格子画白，出现非 0xFF 的格子画红。整片干净时除底部绿条外全白。
 *
 * 屏幕含义：
 *   洋红整屏         读不到芯片（JEDEC ID 全 0xFF 或全 0x00）
 *   白底 + 底部绿条  整片 1 MB 全为 0xFF，即已擦除
 *   白底 + 红色格子  对应区域有数据；底部红条表示非空
 */

#include "fw.h"

void flash_read(uint32_t addr, uint8_t *dst, int n);
void flash_read_id(uint8_t id[3]);

#define TOTAL   (1024u * 1024u)
#define COLS    160
#define ROWS    80
#define CELL    82u

static uint8_t band[COLS * 8 * 2]; /* 每带 8 行，RGB565 高字节在前 */

static void put(int col, int row, uint16_t colour)
{
    int i = ((row & 7) * COLS + col) * 2;
    band[i] = (uint8_t)(colour >> 8);
    band[i + 1] = (uint8_t)colour;
}

static void bar(uint16_t colour)
{
    int y, x;
    uint8_t line[COLS * 2];
    for (x = 0; x < COLS; x++) {
        line[x * 2] = (uint8_t)(colour >> 8);
        line[x * 2 + 1] = (uint8_t)colour;
    }
    for (y = ROWS - 4; y < ROWS; y++)
        lcd_blit(0, y, COLS, 1, line);
}

static int cell_dirty(uint32_t base)
{
    uint8_t b[4];
    static const uint8_t off[4] = { 0, 20, 41, 61 };
    int i;

    for (i = 0; i < 4; i++) {
        uint32_t a = base + off[i];
        if (a >= TOTAL)
            break;
        flash_read(a, b, 1);
        if (b[0] != 0xFFu)
            return 1;
    }
    return 0;
}

int main(void)
{
    uint8_t id[3];
    uint32_t addr;
    uint32_t bad = 0;
    uint8_t buf[256];
    int i;
    int col, row, by;

    RCC->CTLR |= 1u;
    FLASH->ACTLR = (FLASH->ACTLR & ~3u) | 2u;
    RCC->CFGR0 &= 0xF8FFFF0Fu;

    lcd_gpio_init();
    lcd_init();
    lcd_fill(0, 0, COLS, ROWS, 0x0000); /* 扫描期间黑屏 */

    flash_read_id(id);
    if ((id[0] == 0xFFu && id[1] == 0xFFu && id[2] == 0xFFu) ||
        (id[0] == 0x00u && id[1] == 0x00u && id[2] == 0x00u)) {
        lcd_fill(0, 0, COLS, ROWS, 0xF81F);
        for (;;)
            ;
    }

    /* 逐字节扫一遍，统计非 0xFF 的数量 */
    for (addr = 0; addr < TOTAL; addr += sizeof(buf)) {
        int n = (int)(TOTAL - addr);
        if (n > (int)sizeof(buf))
            n = (int)sizeof(buf);
        flash_read(addr, buf, n);
        for (i = 0; i < n; i++)
            if (buf[i] != 0xFFu)
                bad++;
    }

    /* 画格子 */
    for (by = 0; by < ROWS; by += 8) {
        for (i = 0; i < (int)sizeof(band); i += 2) {
            band[i] = 0xFFu;
            band[i + 1] = 0xFFu;
        }
        for (row = by; row < by + 8 && row < ROWS; row++) {
            for (col = 0; col < COLS; col++) {
                uint32_t cell = (uint32_t)row * COLS + (uint32_t)col;
                if (cell_dirty(cell * CELL))
                    put(col, row, 0xF800);
            }
        }
        lcd_blit(0, by, COLS, 8, band);
    }

    bar(bad ? 0xF800 : 0x07E0);

    for (;;)
        ;
    return 0;
}