/* 开机自检画面：不用字库，纯颜色表达。
 * 左边一列 5 个 10x10 方块对应 5 项检查（从上到下：FLASH / TOUCH / LCD / USB / CFG），
 * 右边一个 20x20 大方块是总结果：全绿 = 全部通过，红 = 有失败。
 * 具体哪项叫什么名字，写在 README 里，屏幕上不画字（低分辨率屏画 3x5 字太丑）。 */
#include <stdint.h>
#include "fw.h"

#define CHK_X     56
#define CHK_Y0    10
#define CHK_STEP  13
#define CHK_SIZE  10

#define SUM_X    104
#define SUM_Y     30
#define SUM_SIZE  20

#define COL_OK    0x07E0u    /* 绿：通过 */
#define COL_BAD   0xF800u    /* 红：失败 */
#define COL_WAIT  0x7BEFu    /* 灰：还没测 */
#define COL_BG    0x0000u

void boot_screen_begin(void)
{
    lcd_fill(0, 0, 160, 80, COL_BG);
}

/* state：0=进行中(灰) 1=通过(绿) 2=失败(红) */
void boot_screen_row(uint8_t row, const char *label, uint8_t state)
{
    int y = CHK_Y0 + (int)row * CHK_STEP;
    uint16_t col = (state == 1u) ? COL_OK : (state == 2u) ? COL_BAD : COL_WAIT;

    (void)label;             /* 不用文字，参数保留是为了接口稳定 */
    lcd_fill(CHK_X, y, CHK_SIZE, CHK_SIZE, col);
}

/* 总结果：全绿 / 红。passed/total 只用来判断是否全过 */
void boot_screen_done(uint8_t passed, uint8_t total)
{
    uint16_t col = (passed >= total) ? COL_OK : COL_BAD;

    lcd_fill(SUM_X, SUM_Y, SUM_SIZE, SUM_SIZE, col);
    /* 外框用同色加一圈，远看更像“对/错”的指示灯 */
    lcd_fill(SUM_X - 2, SUM_Y - 2, SUM_SIZE + 4, 2, col);
    lcd_fill(SUM_X - 2, SUM_Y + SUM_SIZE, SUM_SIZE + 4, 2, col);
    lcd_fill(SUM_X - 2, SUM_Y, 2, SUM_SIZE, col);
    lcd_fill(SUM_X + SUM_SIZE, SUM_Y, 2, SUM_SIZE, col);
}
