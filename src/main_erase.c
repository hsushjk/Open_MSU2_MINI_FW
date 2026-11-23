/* 外挂 flash 擦除：整片 1 MB，擦完抽点验证
 *
 * 屏幕状态：
 *   洋红      JEDEC ID 读不到（应为 85 60 14）
 *   深蓝      已启动，正在擦
 *   左下进度条 逐扇区推进，共 256 格
 *   绿        抽点全 0xFF，擦除成功
 *   红        抽点仍有数据，擦除失败
 */
#include "fw.h"
#include "regs.h"

#define FLASH_SIZE  0x100000u
#define SECTOR      0x1000u

int  flash_wait_ready(uint32_t spin);

static int verify_spots(void)
{
    static const uint32_t spots[] = { 0x000000u, 0x040000u, 0x080000u, 0x0FF000u };
    uint8_t buf[16];
    int i, j;

    for (i = 0; i < 4; i++) {
        flash_read(spots[i], buf, 16);
        for (j = 0; j < 16; j++)
            if (buf[j] != 0xFFu)
                return 0;
    }
    return 1;
}

int main(void)
{
    uint8_t id[3];
    uint32_t addr;
    uint32_t done;

    lcd_gpio_init();
    lcd_init();

    flash_read_id(id);
    if (!(id[0] == 0x85u && id[1] == 0x60u && id[2] == 0x14u)) {
        lcd_fill(0, 0, 160, 80, 0xF81F); /* 洋红：ID 不对 */
        for (;;)
            ;
    }

    lcd_fill(0, 0, 160, 80, 0x0010); /* 深蓝：已启动 */

    /* 先试整片擦除。3 000 000 次状态轮询约 6 秒，够 1 MB 擦完。 */
    flash_erase_chip();
    if (flash_wait_ready(3000000u)) {
        if (verify_spots()) {
            lcd_fill(0, 0, 160, 80, 0x07E0);
            for (;;)
                ;
        }
    }

    /* 整片擦除不可用或超时，退化为逐扇区 */
    lcd_fill(0, 0, 160, 80, 0x0010);
    done = 0;
    for (addr = 0; addr < FLASH_SIZE; addr += SECTOR) {
        int w;
        flash_erase_4k(addr);
        flash_wait_ready(200000u);   /* 单扇区擦除上限约 0.4 秒 */
        done++;
        w = (int)((done * 160u) / (FLASH_SIZE / SECTOR));
        if (w > 0)
            lcd_fill(w - 1, 36, 1, 8, 0x07E0);
    }

    if (verify_spots())
        lcd_fill(0, 0, 160, 80, 0x07E0);
    else
        lcd_fill(0, 0, 160, 80, 0xF800);

    for (;;)
        ;

    return 0;
}