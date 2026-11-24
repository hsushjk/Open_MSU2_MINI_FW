/* 免拆自更新：把新固件先写进板载 NOR，再在下次启动时由留在 RAM 里的一份
 * 小程序覆盖内部 flash。
 *
 * 为什么不能直接覆盖：代码正跑在 0x0000 开始的内部 flash 上，擦它的同时
 * 还要从它取指令是不可能的。所以真正动手的那段代码用
 * __attribute__((section(".ramfunc"))) 放进 .data —— 启动时被 crt0 拷到 RAM，
 * 擦掉整个 flash 也不影响它执行。
 *
 * NOR 里的镜像布局（地址 0x080000，避开开机图 0x000000、待机图 0x008000、
 * 设置区 0x0FF000）：
 *   偏移 0 : 'M''S''U''2'
 *   偏移 4 : flags  (bit0 = 镜像有效, bit1 = 请更新)
 *   偏移 6 : len    (小端 32 位)
 *   偏移 10: crc32  (整段镜像，逐位实现，与 assets.c 同一算法)
 *   偏移 14: 保留
 *  偏移 16 : 镜像本体
 */

#include "fw.h"

#define APPIMG_ADDR   0x080000u
#define APPIMG_HDR    16u
#define APPIMG_MAGIC0 'M'
#define APPIMG_MAGIC1 'S'
#define APPIMG_MAGIC2 'U'
#define APPIMG_MAGIC3 '2'
#define APPIMG_FLAG_VALID 0x01u

/* 内部 flash 控制器（regs.h 里的 FLASH_TypeDef 正好对上）
 *   CTLR bit0 PG 编程, bit1 PER 页擦除, bit2 MER 全片擦除, bit6 STRT 启动
 *   STATR bit0 BSY
 */
#define FLASH_CTLR_PG    (1u << 0)
#define FLASH_CTLR_PER   (1u << 1)
#define FLASH_CTLR_MER   (1u << 2)
#define FLASH_CTLR_STRT  (1u << 6)

/* 体检页地址统一在 fw.h 里定义（IFLASH_PROBE_ADDR = 0xF000，应用区止于 0xEFFF） */

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

/* ---- NOR 写：只用于"先把镜像存进去"这一侧 ---- */
static uint32_t stage_addr;   /* 0 表示本次上电还没有成功 begin 过 */
static uint32_t stage_len;    /* 头里声明的镜像长度 */
static uint32_t stage_left;   /* 还能接收多少字节 */

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

    /* 新一轮 begin 作废上一轮没传完的会话：失败时 ready 不会残留为真，
     * 也就不会让后续窗口数据写进一个旧地址。 */
    stage_addr = 0u;
    stage_len = 0u;
    stage_left = 0u;
    if (len == 0u || len > 48000u)
        return 0;
    stage_addr = APPIMG_ADDR + APPIMG_HDR;
    stage_len = len;
    stage_left = len;
    pages = (APPIMG_HDR + len + 4095u) / 4096u;
    for (uint32_t i = 0; i < pages; i++) {
        flash_erase_4k(APPIMG_ADDR + i * 4096u);
        up_iwdg();
    }
    return 1;
}

/* 没有成功 begin 过就丢弃；写了超过声明长度的字节也丢弃。
 * 这两道检查防止 0A 12 02 在窗口命令之前/之后把数据写到 NOR 的 0x000000
 * 开机图或镜像头上去。 */
void update_stage_feed(const uint8_t *src, int n)
{
    if (stage_addr == 0u || stage_len == 0u)
        return;
    if (n <= 0 || (uint32_t)n > stage_left)
        return;
    (void)flash_program(stage_addr, src, n);
    stage_addr += (uint32_t)n;
    stage_left -= (uint32_t)n;
}

int update_stage_ready(void)
{
    return (stage_addr != 0u && stage_left != 0u);
}

int update_stage_commit(uint32_t crc)
{
    uint8_t hdr[APPIMG_HDR];
    uint8_t back[APPIMG_HDR];
    uint8_t buf[64];
    uint32_t off;
    uint32_t calc = 0xFFFFFFFFu;

    /* 没收到声明长度的数据（窗口中途断掉）就没有提交资格：此时 NOR 里的镜像
     * 后半段还是擦除态，CRC 一定对不上，直接判失败让上层回 0x15。 */
    if (stage_addr == 0u || stage_left != 0u)
        return 0;

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
    if (!flash_program(APPIMG_ADDR, hdr, APPIMG_HDR))
        return 0;
    flash_read(APPIMG_ADDR, back, APPIMG_HDR);
    if (back[0] != APPIMG_MAGIC0 || back[4] != APPIMG_FLAG_VALID)
        return 0;
    return 1;
}

/* “请更新”标志单独放在自己的 4 KB 扇区，写它不用碰镜像头 */
#define APPLY_FLAG_ADDR 0x0A0000u

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

/* ---------------- 真正擦写内部 flash 的那一段：跑在 RAM 里 ----------------
 *
 * 寄存器序列按 WCH 官方 SDK（ch32x035_flash.c 的 FLASH_ROM_WRITE /
 * FLASH_ROM_ERASE）的要求，三点必须同时满足：
 *   1) 这颗片的页编程走"页缓冲"：PAGE_PG + BUF_RST 清缓冲，然后 64 个字写到目标
 *      地址、每写一个置 BUF_LOAD 等忙，最后写 ADDR + STRT 才真正烧进 flash；
 *      直接半字写进 flash 是无效的（不报错，但什么也没写进去）。
 *   2) 编程前必须用同样的两个钥匙解 FLASH->MODEKEYR（快速模式），否则页缓冲不工作。
 *   3) 编程地址用 0x0800_0000 别名窗口，而固件代码跑在 0x0000_0000 别名上。
 */

#define FLASH_KEY1      0x45670123u
#define FLASH_KEY2      0xCDEF89ABu

#define CR_PER          (1u << 1)          /* 1 KB 页擦除 */
#define CR_STRT         (1u << 6)          /* 启动 */
#define CR_LOCK         (1u << 7)
#define CR_FLOCK        (1u << 15)         /* 快速模式锁 */
#define CR_OPTER        (1u << 5)
#define CR_PAGE_PG      (1u << 16)         /* 页编程模式 */
#define CR_BUF_LOAD     (1u << 18)         /* 装入页缓冲 */
#define CR_BUF_RST      (1u << 19)         /* 清页缓冲 */
#define CR_PAGE_ER      (1u << 17)         /* 256 字节页擦除 */

#define IFLASH_ALIAS    0x08000000u        /* 内部 flash 的编程别名窗口 */

/* 上次搬运的结果：0=成功，1=头不对，2=长度不对，3=体检失败，4=写回校验失败，
 * 5=整段 CRC 与头里对不上（此时内部 flash 一个字节都没动） */
volatile uint8_t g_up_result __attribute__((section(".noinit")));

static void __attribute__((section(".ramfunc"), noinline)) ram_wait(void)
{
    while ((FLASH->STATR & 1u) != 0u)
        ;
}

static void __attribute__((section(".ramfunc"), noinline)) ram_unlock(void)
{
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
    *(volatile uint32_t *)(0x40022000u + 0x24u) = FLASH_KEY1;
    *(volatile uint32_t *)(0x40022000u + 0x24u) = FLASH_KEY2;
}

static void __attribute__((section(".ramfunc"), noinline)) ram_lock(void)
{
    FLASH->CTLR |= CR_FLOCK;
    FLASH->CTLR |= CR_LOCK;
}

/* 1 KB 页擦除（addr 用别名窗口） */
static void __attribute__((section(".ramfunc"), noinline)) ram_erase_page(uint32_t addr)
{
    FLASH->CTLR &= ~(CR_OPTER | CR_PAGE_ER);
    FLASH->CTLR |= CR_PER;
    FLASH->ADDR = addr;
    FLASH->CTLR |= CR_STRT;
    ram_wait();
    FLASH->CTLR &= ~CR_PER;
}

/* 256 字节页编程：64 个字先落进页缓冲，再一次性提交 */
static void __attribute__((section(".ramfunc"), noinline)) ram_program_page(uint32_t addr,
                                                                            const uint8_t *src)
{
    uint32_t i;
    uint32_t w;

    FLASH->CTLR &= ~(CR_OPTER | CR_PAGE_ER);
    FLASH->CTLR |= CR_PAGE_PG;
    FLASH->CTLR |= CR_BUF_RST;
    ram_wait();

    for (i = 0u; i < 256u; i += 4u) {
        w = (uint32_t)src[i] |
            ((uint32_t)src[i + 1u] << 8) |
            ((uint32_t)src[i + 2u] << 16) |
            ((uint32_t)src[i + 3u] << 24);
        *(volatile uint32_t *)(addr + i) = w;
        FLASH->CTLR |= CR_BUF_LOAD;
        ram_wait();
    }

    FLASH->ADDR = addr;
    FLASH->CTLR |= CR_STRT;
    ram_wait();
    FLASH->CTLR &= ~CR_PAGE_PG;
}

static void __attribute__((section(".ramfunc"), noinline)) ram_reset(void)
{
    *(volatile uint32_t *)0xE000ED0Cu = 0x05FA0004u;
    for (;;)
        ;
}

/* 从 NOR 读一段（内联 SPI，擦掉内部 flash 后也还能用） */
static void __attribute__((section(".ramfunc"), noinline)) ram_nor_read(uint32_t nor_addr,
                                                                        uint8_t *dst,
                                                                        uint32_t n)
{
    uint32_t i;

    ram_dc_input();
    img_cs(0);
    img_spi(0x03);
    img_spi((uint8_t)(nor_addr >> 16));
    img_spi((uint8_t)(nor_addr >> 8));
    img_spi((uint8_t)nor_addr);
    for (i = 0u; i < n; i++)
        dst[i] = img_spi(0xFFu);
    img_cs(1);
}

/* 整段 CRC32（与 assets.c 的 asset_crc32_byte 同一套逐位算法，不查表）。
 * 只读 NOR、不碰内部 flash，所以可以在擦除之前调用；跑在 RAM 里，
 * 将来就算挪到擦除之后也还能用。 */
static uint32_t __attribute__((section(".ramfunc"), noinline))
ram_nor_crc(uint32_t nor_addr, uint32_t len)
{
    uint8_t buf[256];
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t off;

    for (off = 0u; off < len; off += 256u) {
        uint32_t n = len - off;
        uint32_t i;
        uint32_t bit;

        if (n > 256u)
            n = 256u;
        ram_nor_read(nor_addr + off, buf, n);
        for (i = 0u; i < n; i++) {
            crc ^= (uint32_t)buf[i];
            for (bit = 0u; bit < 8u; bit++)
                crc = ((crc & 1u) != 0u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
        up_iwdg();
    }
    return crc ^ 0xFFFFFFFFu;
}

/* 搬运并校验一个 1 KB 页（内部 flash 的擦除粒度）：擦 -> 写 256 字节页 -> 读回比对。
 * 读回不一致就整页重擦重写，最多 3 次；每次重试前后都喂狗。
 * 返回 1 成功，0 三次都失败。跑在 RAM 里，调用方已关中断。 */
static int __attribute__((section(".ramfunc"), noinline)) ram_copy_page(uint32_t off, uint32_t n)
{
    uint8_t buf[256];
    uint32_t attempt;

    for (attempt = 0u; attempt < 3u; attempt++) {
        uint32_t p;
        uint32_t i;
        int ok = 1;

        ram_erase_page(IFLASH_ALIAS + off);
        ram_iwdg();
        for (p = 0u; p < n; p += 256u) {
            ram_nor_read(APPIMG_ADDR + APPIMG_HDR + off + p, buf, 256u);
            ram_program_page(IFLASH_ALIAS + off + p, buf);
            ram_iwdg();
        }
        for (p = 0u; p < n && ok; p += 256u) {
            ram_nor_read(APPIMG_ADDR + APPIMG_HDR + off + p, buf, 256u);
            for (i = 0u; i < 256u; i++) {
                if (*(volatile uint8_t *)(IFLASH_ALIAS + off + p + i) != buf[i]) {
                    ok = 0;
                    break;
                }
            }
        }
        if (ok)
            return 1;
        ram_iwdg();
    }
    return 0;
}

/* 失败收尾：锁回 flash、PA6 恢复推挽输出、结果写进 .noinit、中断重新打开。
 * 任何要 return 的失败路径都先走这里，绝不能带着关中断或 PA6 输入态返回。 */
static void __attribute__((section(".ramfunc"), noinline)) up_apply_abort(uint8_t result)
{
    ram_lock();
    ram_dc_output();
    g_up_result = result;
    __asm__ volatile("csrsi mstatus, 8");
}

int __attribute__((section(".ramfunc"), noinline)) update_apply(void)
{
    uint8_t hdr[APPIMG_HDR];
    uint8_t buf[256];
    uint32_t off;
    uint32_t len;
    uint32_t i;
    uint32_t crc_want;

    /* 1. 读镜像头 */
    ram_nor_read(APPIMG_ADDR, hdr, APPIMG_HDR);
    if (hdr[0] != APPIMG_MAGIC0 || hdr[1] != APPIMG_MAGIC1 ||
        hdr[2] != APPIMG_MAGIC2 || hdr[3] != APPIMG_MAGIC3) {
        ram_dc_output();
        g_up_result = 1u;
        return 1;
    }
    len = (uint32_t)hdr[6] | ((uint32_t)hdr[7] << 8) |
          ((uint32_t)hdr[8] << 16) | ((uint32_t)hdr[9] << 24);
    if (len == 0u || len > 48000u || (len & 0xFFu) != 0u) {
        ram_dc_output();
        g_up_result = 2u;
        return 2;
    }

    /* 2. 整段 CRC 预检：把 NOR 里的镜像再逐块读一遍算 CRC32，与头里的比。
     *    对不上就直接返回，**绝不擦内部 flash**：擦掉能跑的旧固件又搬不进新固件
     *    就是变砖。这一步只读 NOR（SPI 位操作在 RAM 里），中断照常开着。 */
    crc_want = (uint32_t)hdr[10] | ((uint32_t)hdr[11] << 8) |
               ((uint32_t)hdr[12] << 16) | ((uint32_t)hdr[13] << 24);
    if (ram_nor_crc(APPIMG_ADDR + APPIMG_HDR, len) != crc_want) {
        ram_dc_output();
        g_up_result = 5u;
        return 5;
    }

    /* 3. 体检：擦 + 写 + 读回，只要时序不对就在这里失败，内部 flash 一点没动 */
    ram_unlock();
    ram_erase_page(IFLASH_ALIAS + IFLASH_PROBE_ADDR);
    for (i = 0u; i < 256u; i++)
        buf[i] = (uint8_t)(i ^ 0x5Au);
    ram_program_page(IFLASH_ALIAS + IFLASH_PROBE_ADDR, buf);
    for (i = 0u; i < 256u; i++) {
        if (*(volatile uint8_t *)(IFLASH_ALIAS + IFLASH_PROBE_ADDR + i) != buf[i]) {
            ram_dc_output();
            g_up_result = 3u;
            return 3;
        }
    }
    ram_erase_page(IFLASH_ALIAS + IFLASH_PROBE_ADDR);
    ram_dc_output();

    /* 4. 逐页搬运：每页先擦再写再读回，页与页之间互相独立。
     *    一开始就擦掉整片的话，任何一次失败都会留下“全空 + 回不去”的状态；
     *    按页擦写至少让前面的页已经是新镜像。从这里开始关中断、只跑 RAM 里的代码。 */
    __asm__ volatile("csrci mstatus, 8");

    for (off = 0u; off < len; off += 1024u) {
        uint32_t n = len - off;

        if (n > 1024u)
            n = 1024u;
        if (!ram_copy_page(off, n)) {
            up_apply_abort(4u);
            return 4;
        }
    }
    ram_lock();

    /* 6. 复位，让新固件跑起来 */
    g_up_result = 0u;
    ram_reset();
    return 0;
}
