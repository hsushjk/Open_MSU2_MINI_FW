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
#define IFLASH_PROBE_ADDR 0x7000u   /* 28 KB：图只有 17 KB，若 flash 只有 32 KB 这里也在范围内 */

static void up_iwdg(void)
{
    *(volatile uint32_t *)0x40003000u = 0xAAAAu;
}

/* ---- 很小的 NOR 读（复用以太先初始化好的 SPI1 与 PB7 片选） ---- */
static void __attribute__((section(".ramfunc"), noinline)) img_cs(int level)
{
    if (level)
        GPIOB->BSHR = 0x080u;
    else
        GPIOB->BCR = 0x080u;
}

static uint8_t __attribute__((section(".ramfunc"), noinline)) img_spi(uint8_t out)
{
    while ((SPI1->STATR & 0x02u) == 0u)
        ;
    SPI1->DATAR = out;
    while ((SPI1->STATR & 0x01u) == 0u)
        ;
    return (uint8_t)SPI1->DATAR;
}

/* PA6 兼 flash MISO 与屏 D/C：读 flash 前切成浮空输入，读完切回推挽输出。
   gpio_mode() 在 flash 里，全片擦除之后不能调，所以这里直接写 CFGLR。 */
static void __attribute__((section(".ramfunc"), noinline)) ram_dc_input(void)
{
    uint32_t v = GPIOA->CFGLR;

    v &= ~(0xFu << 24);
    v |= (0x4u << 24);
    GPIOA->CFGLR = v;
}

static void __attribute__((section(".ramfunc"), noinline)) ram_dc_output(void)
{
    uint32_t v = GPIOA->CFGLR;

    v &= ~(0xFu << 24);
    v |= (0x2u << 24);
    GPIOA->CFGLR = v;
}

/* up_iwdg() 在 flash 里，擦完就没得跑了，编程循环里用这个喂狗 */
static void __attribute__((section(".ramfunc"), noinline)) ram_iwdg(void)
{
    *(volatile uint32_t *)0x40003000u = 0xAAAAu;
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

/* 这颗片的 flash 控制器在每次擦/写结束后会自己把 LOCK 位重新置上，
   所以每个操作之前都得重新解一次锁，否则后面的操作用静默忽略。 */
static void __attribute__((section(".ramfunc"), noinline)) ram_ensure_unlock(void)
{
    uint32_t i;

    for (i = 0; i < 3u && (FLASH->CTLR & 0x80u) != 0u; i++)
    {
        FLASH->KEYR = 0x45670123u;
        FLASH->KEYR = 0xCDEF89ABu;
    }
}

static void __attribute__((section(".ramfunc"), noinline)) ram_wait(void)
{
    while (FLASH->STATR & 1u)
        ;
}

static int __attribute__((section(".ramfunc"), noinline)) ram_program_page(uint32_t addr,
                                                                       const uint8_t *src,
                                                                       uint32_t n)
{
    uint32_t i;

    /* CH32 内部 flash 只支持半字写入：写一个 16 位数、等 BSY 落下，再写下一个。
       编程模式不需要 STRT（那是擦除用的启动位）。 */
    ram_ensure_unlock();
    FLASH->CTLR |= FLASH_CTLR_PG;
    for (i = 0; i + 1u < n; i += 2u) {
        uint16_t h = (uint16_t)((uint16_t)src[i] | ((uint16_t)src[i + 1u] << 8));

        *(volatile uint16_t *)(addr + i) = h;
        ram_wait();
    }
    FLASH->CTLR &= ~FLASH_CTLR_PG;
    return 0;
}

static void __attribute__((section(".ramfunc"), noinline)) ram_erase_page(uint32_t addr)
{
    ram_ensure_unlock();
    FLASH->CTLR |= FLASH_CTLR_PER;
    FLASH->ADDR = addr;
    FLASH->CTLR |= FLASH_CTLR_STRT;
    ram_wait();
    FLASH->CTLR &= ~FLASH_CTLR_PER;
}

static void __attribute__((section(".ramfunc"), noinline)) ram_erase_all(void)
{
    ram_ensure_unlock();
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
/* 上次搬运的结果：0=成功，1=头不对，2=长度不对，3=体检失败，4=写回校验失败 */
volatile uint8_t g_up_result __attribute__((section(".noinit")));

/* v72：走 WCH 的“快速编程”通道（MODEKEYR 解模式锁 + FTPG 缓冲 + FTER 触发） */
volatile uint8_t g_diag[10] __attribute__((section(".noinit")));

#define FLASH_MODEKEYR (*(volatile uint32_t *)0x40022024u)
#define FLASH_CTLR_FTPG (1u << 16)
#define FLASH_CTLR_FTER (1u << 17)

static void __attribute__((section(".ramfunc"), noinline)) burn(void)
{
    volatile uint32_t d;

    for (d = 0; d < 24000u; d++)
        ;
}

static void __attribute__((section(".ramfunc"), noinline)) mode_unlock(void)
{
    FLASH_MODEKEYR = 0x45670123u;
    FLASH_MODEKEYR = 0xCDEF89ABu;
}

static uint8_t __attribute__((section(".ramfunc"), noinline)) try_fast(uint32_t base)
{
    uint32_t i;

    ram_unlock();
    ram_erase_page(base);
    burn();
    mode_unlock();
    g_diag[0] = (uint8_t)((FLASH->CTLR >> 16) & 0xFFu);
    FLASH->CTLR |= FLASH_CTLR_FTPG;
    for (i = 0; i < 8u; i += 4u)
        *(volatile uint32_t *)(base + i) = 0xA0A1A2A3u ^ i;
    FLASH->CTLR |= FLASH_CTLR_FTER;
    burn();
    burn();
    FLASH->CTLR &= ~(FLASH_CTLR_FTPG | FLASH_CTLR_FTER);
    burn();
    return *(volatile uint8_t *)base;
}

void __attribute__((section(".ramfunc"), noinline)) update_diag(void)
{
    g_diag[7] = *(volatile uint8_t *)IFLASH_PROBE_ADDR;
    g_diag[1] = try_fast(IFLASH_PROBE_ADDR);
    g_diag[2] = try_fast(0x5000u);
    g_diag[3] = (uint8_t)(FLASH->STATR & 0xFFu);
    g_diag[4] = (uint8_t)((FLASH->STATR >> 8) & 0xFFu);
    g_diag[5] = (uint8_t)(FLASH->CTLR & 0xFFu);
    g_diag[6] = (uint8_t)((FLASH->CTLR >> 8) & 0xFFu);
    ram_erase_page(IFLASH_PROBE_ADDR);
    ram_erase_page(0x5000u);
}

int __attribute__((section(".ramfunc"), noinline)) update_apply(void)
{
    uint8_t hdr[APPIMG_HDR];
    uint8_t buf[256];
    uint8_t chk[256];
    uint32_t off;
    uint32_t len;
    int i;


    /* 1. 读镜像头（内联 SPI 读，不走 flash 里的驱动） */
    ram_dc_input();
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
    {
        g_up_result = 1u;
        return 1;
    }
    len = (uint32_t)hdr[6] | ((uint32_t)hdr[7] << 8) |
          ((uint32_t)hdr[8] << 16) | ((uint32_t)hdr[9] << 24);
    if (len == 0u || len > 48000u)
    {
        g_up_result = 2u;
        return 2;
    }

    /* 2. 先在用不到的高段做一次“擦-写-读回”体检：
     *    页大小/编程时序只要有一处不对，这里就会失败，此时内部 flash 一点没动，
     *    设备照常用老固件跑，绝不会变砖。 */
    ram_unlock();
    ram_erase_page(IFLASH_PROBE_ADDR);
    for (i = 0; i < 64; i++)
        buf[i] = (uint8_t)(i ^ 0x5Au);
    ram_program_page(IFLASH_PROBE_ADDR, buf, 64u);
    for (i = 0; i < 64; i++)
        chk[i] = *(volatile uint8_t *)(IFLASH_PROBE_ADDR + (uint32_t)i);
    for (i = 0; i < 64; i++)
        if (chk[i] != buf[i])
        {
            g_up_result = 3u;
            return 3;
        }
    ram_erase_page(IFLASH_PROBE_ADDR);

    /* 3. 全片擦除内部 flash，再按 256 字节一页写回镜像 */
    __asm__ volatile("csrci mstatus, 8");   /* 从这里开始向量表就要没了，关中断 */
    ram_erase_all();
    ram_iwdg();

    for (off = 0u; off < len; off += 256u) {
        uint32_t n = (len - off > 256u) ? 256u : (len - off);

        /* 从 NOR 读一页（内联） */
        ram_dc_input();
        img_cs(0);
        img_spi(0x03);
        img_spi((uint8_t)((APPIMG_ADDR + APPIMG_HDR + off) >> 16));
        img_spi((uint8_t)((APPIMG_ADDR + APPIMG_HDR + off) >> 8));
        img_spi((uint8_t)(APPIMG_ADDR + APPIMG_HDR + off));
        for (i = 0; i < (int)n; i++)
            buf[i] = img_spi(0xFF);
        img_cs(1);
        ram_dc_output();
        for (i = (int)n; i < 256; i++)
            buf[i] = 0xFFu;

        ram_program_page(off, buf, 256u);
        ram_iwdg();

        for (i = 0; i < (int)n; i++)
            chk[i] = *(volatile uint8_t *)(off + (uint32_t)i);
        for (i = 0; i < (int)n; i++)
            if (chk[i] != buf[i])
            {
                g_up_result = 4u;
                return 4;
            }
    }

    /* 4. 复位，让新固件跑起来 */
    ram_reset();
    return 0;
}