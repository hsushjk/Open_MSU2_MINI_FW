/* 免拆自更新：把新固件先写进板载 NOR，再在下次启动时由留在 RAM 里的一份
 * 小程序覆盖内部 flash。
 *
 * 为什么不能直接覆盖：代码正跑在 0x0000 开始的内部 flash 上，擦它的同时
 * 还要从它取指令是不可能的。所以真正动手的那段代码用
 * __attribute__((section(".ramfunc"))) 放进 .data —— 启动时被 crt0 拷到 RAM，
 * 擦掉整个 flash 也不影响它执行。
 *
 * NOR 里的镜像布局（地址 0x060000，避开开机图 0x000000、待机图 0x010000、
 * 设置区 0x0FF000）：
 *   偏移 0 : 'M''S''U''2'
 *   偏移 4 : flags  (bit0 = 镜像有效, bit1 = 请更新)
 *   偏移 6 : len    (小端 32 位)
 *   偏移 10: crc32  (整段镜像，逐位实现，与 assets.c 同一算法)
 *   偏移 14: 保留
 *  偏移 16 : 镜像本体
 */

#include "fw.h"

#define APPIMG_ADDR   0x060000u
#define APPIMG_HDR    16u
#define APPIMG_MAGIC0 'M'
#define APPIMG_MAGIC1 'S'
#define APPIMG_MAGIC2 'U'
#define APPIMG_MAGIC3 '2'
#define APPIMG_FLAG_VALID 0x01u
#define APPIMG_FLAG_APPLY 0x02u

/* 内部 flash 控制器（regs.h 里的 FLASH_TypeDef 正好对上）
 *   CTLR bit0 PG 编程, bit1 PER 页擦除, bit2 MER 全片擦除, bit6 STRT 启动
 *   STATR bit0 BSY
 */
#define FLASH_CTLR_PG    (1u << 0)
#define FLASH_CTLR_PER   (1u << 1)
#define FLASH_CTLR_MER   (1u << 2)
#define FLASH_CTLR_STRT  (1u << 6)

/* 每次启动都会跑的探测区间（内部 flash 高段，应用只有 ~15 KB 用不到这里） */
#define IFLASH_PROBE_ADDR 0x8000u

static void up_iwdg(void)
{
    *(volatile uint32_t *)0x40003000u = 0xAAAAu;
}

/* ---- 很小的 NOR 读（复用以太先初始化好的 SPI1 与 PB7 片选） ---- */
static void img_cs(int level)
{
    if (level)
        GPIOB->BSHR = 0x080u;
    else
        GPIOB->BCR = 0x080u;
}

static uint8_t img_spi(uint8_t out)
{
    while ((SPI1->STATR & 0x02u) == 0u)
        ;
    SPI1->DATAR = out;
    while ((SPI1->STATR & 0x01u) == 0u)
        ;
    return (uint8_t)SPI1->DATAR;
}

/* ---- NOR 写：只用于“先把镜像存进去”这一侧 ---- */
static uint32_t stage_addr;
static uint32_t stage_len;

static void put32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int update_stage_begin(uint32_t len)
{
    uint32_t pages;

    if (len == 0u || len > 48000u)
        return 0;
    stage_addr = APPIMG_ADDR + APPIMG_HDR;
    stage_len = len;
    pages = (APPIMG_HDR + len + 4095u) / 4096u;
    for (uint32_t i = 0; i < pages; i++) {
        flash_erase_4k(APPIMG_ADDR + i * 4096u);
        up_iwdg();
    }
    return 1;
}

void update_stage_feed(const uint8_t *src, int n)
{
    flash_program(stage_addr, src, n);
    stage_addr += (uint32_t)n;
}

int update_stage_commit(uint32_t crc)
{
    uint8_t hdr[APPIMG_HDR];
    uint8_t back[APPIMG_HDR];
    uint8_t buf[64];
    uint32_t off;
    uint32_t calc = 0xFFFFFFFFu;

    /* 写完再读回来算一遍 CRC：写坏就当场拒绝，别等重启时才发现 */
    for (off = 0; off < stage_len; ) {
        int n = (int)((stage_len - off > sizeof(buf)) ? sizeof(buf) : (stage_len - off));

        flash_read(APPIMG_ADDR + APPIMG_HDR + off, buf, n);
        calc = asset_crc32_bytes(calc, buf, n);
        off += (uint32_t)n;
        up_iwdg();
    }
    calc ^= 0xFFFFFFFFu;
    if (calc != crc) {
        flash_erase_4k(APPIMG_ADDR);
        return 0;
    }

    hdr[0] = APPIMG_MAGIC0;
    hdr[1] = APPIMG_MAGIC1;
    hdr[2] = APPIMG_MAGIC2;
    hdr[3] = APPIMG_MAGIC3;
    hdr[4] = APPIMG_FLAG_VALID;
    hdr[5] = 0u;
    put32le(hdr + 6, stage_len);
    put32le(hdr + 10, crc);
    flash_program(APPIMG_ADDR, hdr, APPIMG_HDR);
    flash_read(APPIMG_ADDR, back, APPIMG_HDR);
    if (back[0] != APPIMG_MAGIC0 || back[4] != APPIMG_FLAG_VALID)
        return 0;
    return 1;
}

/* “请更新”标志单独放在自己的 4 KB 扇区，写它不用碰镜像头 */
#define APPLY_FLAG_ADDR 0x070000u

void update_set_apply_flag(void)
{
    uint8_t v = 0xA5u;

    flash_erase_4k(APPLY_FLAG_ADDR);
    flash_program(APPLY_FLAG_ADDR, &v, 1);
}

void update_clear_apply_flag(void)
{
    flash_erase_4k(APPLY_FLAG_ADDR);
}

static int apply_flag_set(void)
{
    uint8_t v = 0u;

    flash_read(APPLY_FLAG_ADDR, &v, 1);
    return (v == 0xA5u) ? 1 : 0;
}

int update_pending(void)
{
    uint8_t hdr[APPIMG_HDR];

    if (!apply_flag_set())
        return 0;
    flash_read(APPIMG_ADDR, hdr, APPIMG_HDR);
    if (hdr[0] != APPIMG_MAGIC0 || hdr[1] != APPIMG_MAGIC1 ||
        hdr[2] != APPIMG_MAGIC2 || hdr[3] != APPIMG_MAGIC3)
        return 0;
    if ((hdr[4] & APPIMG_FLAG_VALID) == 0u)
        return 0;
    return 1;
}

/* ---------------- 真正擦写内部 flash 的那一段：跑在 RAM 里 ---------------- */

/* 内部 flash 解锁（KEYR 在 +0x04） */
static void __attribute__((section(".ramfunc"), noinline)) ram_unlock(void)
{
    FLASH->KEYR = 0x45670123u;
    FLASH->KEYR = 0xCDEF89ABu;
}

static void __attribute__((section(".ramfunc"), noinline)) ram_wait(void)
{
    while (FLASH->STATR & 1u)
        ;
}

static int __attribute__((section(".ramfunc"), noinline)) ram_program_page(uint32_t addr,
                                                                       const uint8_t *src)
{
    volatile uint32_t *dst = (volatile uint32_t *)addr;
    int i;

    FLASH->CTLR |= FLASH_CTLR_PG;
    FLASH->CTLR |= FLASH_CTLR_STRT;
    ram_wait();
    for (i = 0; i < 64; i++) {
        uint32_t w = (uint32_t)src[i * 4] | ((uint32_t)src[i * 4 + 1] << 8) |
                     ((uint32_t)src[i * 4 + 2] << 16) | ((uint32_t)src[i * 4 + 3] << 24);

        dst[i] = w;
    }
    ram_wait();
    FLASH->CTLR &= ~FLASH_CTLR_PG;
    return 0;
}

static void __attribute__((section(".ramfunc"), noinline)) ram_erase_page(uint32_t addr)
{
    FLASH->CTLR |= FLASH_CTLR_PER;
    FLASH->ADDR = addr;
    FLASH->CTLR |= FLASH_CTLR_STRT;
    ram_wait();
    FLASH->CTLR &= ~FLASH_CTLR_PER;
}

static void __attribute__((section(".ramfunc"), noinline)) ram_erase_all(void)
{
    FLASH->CTLR |= FLASH_CTLR_MER;
    FLASH->CTLR |= FLASH_CTLR_STRT;
    ram_wait();
    FLASH->CTLR &= ~FLASH_CTLR_MER;
}

static void __attribute__((section(".ramfunc"), noinline)) ram_reset(void)
{
    *(volatile uint32_t *)0xE000ED0Cu = 0x05FA0004u;
    for (;;)
        ;
}

/* 全在 RAM 里：不调用任何 flash 里的函数（包括 spi_byte / flash_read） */
int __attribute__((section(".ramfunc"), noinline)) update_apply(void)
{
    uint8_t hdr[APPIMG_HDR];
    uint8_t buf[256];
    uint8_t chk[256];
    uint32_t off;
    uint32_t len;
    int i;

    __asm__ volatile("csrci mstatus, 8");       /* 关中断：向量表马上就要没了 */

    /* 1. 读镜像头（内联 SPI 读，不走 flash 里的驱动） */
    lcd_dc_as_input();
    img_cs(0);
    img_spi(0x03);
    img_spi((uint8_t)(APPIMG_ADDR >> 16));
    img_spi((uint8_t)(APPIMG_ADDR >> 8));
    img_spi((uint8_t)APPIMG_ADDR);
    for (i = 0; i < (int)APPIMG_HDR; i++)
        hdr[i] = img_spi(0xFF);
    img_cs(1);

    if (hdr[0] != APPIMG_MAGIC0 || hdr[1] != APPIMG_MAGIC1 ||
        hdr[2] != APPIMG_MAGIC2 || hdr[3] != APPIMG_MAGIC3)
        return 1;
    len = (uint32_t)hdr[6] | ((uint32_t)hdr[7] << 8) |
          ((uint32_t)hdr[8] << 16) | ((uint32_t)hdr[9] << 24);
    if (len == 0u || len > 48000u)
        return 2;

    /* 2. 先在用不到的高段做一次“擦-写-读回”体检：
     *    页大小/编程时序只要有一处不对，这里就会失败，此时内部 flash 一点没动，
     *    设备照常用老固件跑，绝不会变砖。 */
    ram_unlock();
    ram_erase_page(IFLASH_PROBE_ADDR);
    for (i = 0; i < 256; i++)
        buf[i] = (uint8_t)(i ^ 0x5Au);
    ram_program_page(IFLASH_PROBE_ADDR, buf);
    for (i = 0; i < 256; i++)
        chk[i] = *(volatile uint8_t *)(IFLASH_PROBE_ADDR + (uint32_t)i);
    for (i = 0; i < 256; i++)
        if (chk[i] != buf[i])
            return 3;
    ram_erase_page(IFLASH_PROBE_ADDR);

    /* 3. 全片擦除内部 flash，再按 256 字节一页写回镜像 */
    ram_erase_all();

    for (off = 0u; off < len; off += 256u) {
        uint32_t n = (len - off > 256u) ? 256u : (len - off);

        /* 从 NOR 读一页（内联） */
        lcd_dc_as_input();
        img_cs(0);
        img_spi(0x03);
        img_spi((uint8_t)((APPIMG_ADDR + APPIMG_HDR + off) >> 16));
        img_spi((uint8_t)((APPIMG_ADDR + APPIMG_HDR + off) >> 8));
        img_spi((uint8_t)(APPIMG_ADDR + APPIMG_HDR + off));
        for (i = 0; i < (int)n; i++)
            buf[i] = img_spi(0xFF);
        img_cs(1);
        lcd_dc_as_output();
        for (i = (int)n; i < 256; i++)
            buf[i] = 0xFFu;

        ram_program_page(off, buf);

        for (i = 0; i < (int)n; i++)
            chk[i] = *(volatile uint8_t *)(off + (uint32_t)i);
        for (i = 0; i < (int)n; i++)
            if (chk[i] != buf[i])
                return 4;
    }

    /* 4. 复位，让新固件跑起来 */
    ram_reset();
    return 0;
}