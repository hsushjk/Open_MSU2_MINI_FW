/* 板载 NOR（P25D80H，1 MB）上的“资产”：开机画面 + 待机画面，外加一小块设置。
 *
 * 分区（32 KB 对齐，方便以后换压缩格式）：
 *   0x000000  开机画面   1 槽
 *   0x010000  待机画面   8 槽（0x010000 + idx * 0x8000）
 *   0x0FF000  设置区     32 字节
 *
 * 每槽开头 16 字节头：
 *   'M''S''A''1' | w(2) | h(2) | fmt(1) | flags(1) | len(4) | crc32(4) | pad(2)
 * 之后是 len 字节像素（RGB565 小端）。
 *
 * 写入是流式的：begin(kind, idx, len) → 推 len 个字节 → commit(crc32)。
 * 写之前先擦干净；数据写完才写头，所以中途断电的槽仍然算空。
 */

#include "fw.h"

#define ASSET_MAGIC   0x3141534Du   /* "MSA1" */
#define CFG_MAGIC     0x3153324Du   /* "MS2S" */
#define CFG_ADDR      0x0FF000u
#define BOOT_ADDR     0x000000u
#define STANDBY_BASE  0x010000u
#define SLOT_SIZE     0x00008000u
#define HDR_SIZE      20u
#define PAGE_SIZE     256u

static uint8_t  wr_active;
static uint8_t  wr_kind;
static uint8_t  wr_idx;
static uint32_t wr_len;         /* 本槽应有的数据长度 */
static uint32_t wr_addr;        /* 下一个字节写到哪 */
static uint32_t wr_left;
static uint32_t wr_crc;
static uint8_t  wr_buf[PAGE_SIZE];
static uint32_t wr_fill;
static uint8_t  slot_ok;        /* 待机槽的“已校验”位图 */
static uint8_t  boot_ok;        /* 开机图已校验 */

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t asset_slot_addr(uint8_t kind, uint8_t idx)
{
    if (kind == 0u)
        return BOOT_ADDR;
    if (idx >= STANDBY_SLOTS)
        idx = 0u;
    return STANDBY_BASE + (uint32_t)idx * SLOT_SIZE;
}

uint32_t asset_max_len(void)
{
    return SLOT_SIZE - HDR_SIZE;
}

/* ---------- 流式写入 ---------- */

static void wr_flush_page(void)
{
    if (wr_fill == 0u)
        return;
    flash_program(wr_addr, wr_buf, (int)wr_fill);
    wr_addr += wr_fill;
    wr_fill = 0u;
}

void asset_begin(uint8_t kind, uint8_t idx, uint32_t len)
{
    uint32_t base = asset_slot_addr(kind, idx);
    uint32_t a;

    wr_active = 0u;
    if (len == 0u || len > asset_max_len())
        return;

    for (a = base; a < base + HDR_SIZE + len; a += 4096u) {
        flash_erase_4k(a);
        *(volatile uint32_t *)0x40003000u = 0xAAAAu;   /* 擦得慢，顺手喂狗 */
    }

    wr_kind = kind;
    wr_idx = idx;
    wr_len = len;
    wr_addr = base + HDR_SIZE;
    wr_left = len;
    wr_fill = 0u;
    wr_crc = 0xFFFFFFFFu;
    wr_active = 1u;
    slot_ok = 0u;
    boot_ok = 0u;
}

void asset_feed(const uint8_t *src, int n)
{
    int i;

    if (!wr_active)
        return;
    for (i = 0; i < n && wr_left > 0u; i++) {
        wr_crc = asset_crc32_byte(wr_crc, src[i]);
        wr_buf[wr_fill++] = src[i];
        wr_left--;
        if (wr_fill == PAGE_SIZE)
            wr_flush_page();
        if ((wr_left & 0x3FFu) == 0u)
            *(volatile uint32_t *)0x40003000u = 0xAAAAu;
    }
    if (wr_left == 0u)
        wr_flush_page();
}

void asset_abort(void)
{
    wr_active = 0u;
    wr_left = 0u;
    wr_fill = 0u;
}

int asset_writing(void)
{
    return wr_active ? 1 : 0;
}

uint32_t asset_left(void)
{
    return wr_left;
}

int asset_finish(uint32_t expect_crc)
{
    uint32_t crc = wr_crc ^ 0xFFFFFFFFu;
    uint8_t hdr[HDR_SIZE];
    uint32_t base;

    if (!wr_active)
        return 0;
    wr_flush_page();
    wr_active = 0u;
    if (wr_left != 0u || crc != expect_crc)
        return 0;

    base = asset_slot_addr(wr_kind, wr_idx);
    put32(hdr, ASSET_MAGIC);
    hdr[4] = 160u; hdr[5] = 0u;      /* 宽 */
    hdr[6] = 80u;  hdr[7] = 0u;      /* 高 */
    hdr[8] = 0u;                     /* fmt: RGB565 */
    hdr[9] = 0u;
    put32(hdr + 10, wr_len);
    put32(hdr + 14, crc);
    flash_program(base, hdr, (int)HDR_SIZE);
    return 1;
}

void asset_erase(uint8_t kind, uint8_t idx)
{
    uint32_t base = asset_slot_addr(kind, idx);

    wr_active = 0u;
    flash_erase_4k(base);
    slot_ok = 0u;
    boot_ok = 0u;
}

/* ---------- 读 ---------- */

static int hdr_ok(uint8_t kind, uint8_t idx)
{
    uint8_t hdr[HDR_SIZE];

    flash_read(asset_slot_addr(kind, idx), hdr, (int)HDR_SIZE);
    if (get32(hdr) != ASSET_MAGIC)
        return 0;
    if (get16(hdr + 4) != 160u || get16(hdr + 6) != 80u)
        return 0;
    if (hdr[8] != 0u)
        return 0;
    if (get32(hdr + 10) != 160u * 80u * 2u)
        return 0;
    return 1;
}

/* 校验 CRC（读一整帧≈25 KB，只在需要时做，结果缓存在 slot_ok 里） */
int asset_valid(uint8_t kind, uint8_t idx)
{
    uint8_t hdr[HDR_SIZE];
    uint8_t buf[64];
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t left;
    uint32_t addr;
    uint8_t bit = (uint8_t)(1u << (idx & 7u));

    if (!hdr_ok(kind, idx))
        return 0;
    if (kind == 0u) {
        if (boot_ok)
            return 1;
    } else if (slot_ok & bit) {
        return 1;
    }

    flash_read(asset_slot_addr(kind, idx), hdr, (int)HDR_SIZE);
    addr = asset_slot_addr(kind, idx) + HDR_SIZE;
    left = get32(hdr + 10);
    while (left > 0u) {
        uint32_t n = left > sizeof(buf) ? (uint32_t)sizeof(buf) : left;
        uint32_t i;

        flash_read(addr, buf, (int)n);
        for (i = 0u; i < n; i++)
            crc = asset_crc32_byte(crc, buf[i]);
        addr += n;
        left -= n;
        *(volatile uint32_t *)0x40003000u = 0xAAAAu;
    }
    if ((crc ^ 0xFFFFFFFFu) != get32(hdr + 14))
        return 0;
    if (kind == 0u)
        boot_ok = 1u;
    else
        slot_ok = (uint8_t)(slot_ok | bit);
    return 1;
}

/* 把槽里的图画到屏上（左上角对齐，160x80 正好整屏） */
int asset_draw(uint8_t kind, uint8_t idx)
{
    static uint8_t row[640];         /* 4 行像素 */

    if (!asset_valid(kind, idx))
        return 0;

    /*
     * 这块板子上 SPI 是屏和 flash 共用的，而且 PA6 一脚兼 flash 的 MISO 与屏的 D/C。
     * 所以**不能**在开流（CS_LCD 低）的状态下去读 flash：读 flash 的时钟会同时打进屏里，
     * 屏会把 0x03、地址、dummy 当成命令/数据，画面就只剩开头一小条。
     *
     * 正确做法：一条 2 行的带子 = 640 字节，
     *   1) 屏 CS 高，读 flash 到 RAM；
     *   2) 按这条带子重设窗口（顺带 CS 低、D/C=1），把 640 字节推出去；
     *   3) 等 DMA 走完、CS 拉高，再读下一段。
     * 窗口每条带子单独设，也就不怕 RAM 指针在 CS 翻转后失效。
     */
    {
        uint32_t addr = asset_slot_addr(kind, idx) + HDR_SIZE;
        int y;

        for (y = 0; y < 80; y += 2) {
            flash_read(addr, row, (int)sizeof(row));      /* 此时屏未选中 */
            addr += sizeof(row);
            lcd_stream_begin(0, y, 160, y + 2);           /* 只覆盖这 2 行 */
            lcd_raw_write(row, sizeof(row));
            lcd_raw_idle();                               /* 等 DMA、抬 CS */
        }
    }
    return 1;
}

/* ---------- 设置区（32 字节） ---------- */

void settings_default(settings_t *s)
{
    s->boot_mode = 0u;        /* 默认不画开机画面 */
    s->standby_mode = 1u;     /* 默认四角方块 */
    s->standby_count = STANDBY_SLOTS;   /* 轮播时遍历全部槽位，空槽自动跳过 */
    s->dwell = 3u;
    s->boot_hold = 3u;        /* 开机画面停留 3 秒 */
}

int settings_load(settings_t *s)
{
    uint8_t b[32];
    uint32_t crc;

    flash_read(CFG_ADDR, b, 32);
    if (get32(b) != CFG_MAGIC || b[4] != 1u) {
        settings_default(s);
        return 0;
    }
    crc = asset_crc32_bytes(0xFFFFFFFFu, b, 22) ^ 0xFFFFFFFFu;
    if (crc != get32(b + 28)) {
        settings_default(s);
        return 0;
    }
    s->boot_mode = b[5];
    s->standby_mode = b[6];
    s->standby_count = b[7];
    if (s->standby_count == 0u || s->standby_count > STANDBY_SLOTS)
        s->standby_count = 1u;
    s->dwell = (uint16_t)((uint16_t)b[8] | ((uint16_t)b[9] << 8));
    s->boot_hold = b[10];
    if (s->boot_hold < 1u || s->boot_hold > 60u)
        s->boot_hold = 4u;
    if (s->dwell < 1u)
        s->dwell = 3u;
    return 1;
}

int settings_save(const settings_t *s)
{
    uint8_t b[32];
    uint32_t crc;
    int i;

    for (i = 0; i < 32; i++)
        b[i] = 0u;
    put32(b, CFG_MAGIC);
    b[4] = 1u;
    b[5] = s->boot_mode;
    b[6] = s->standby_mode;
    b[7] = s->standby_count;
    b[8] = (uint8_t)(s->dwell & 0xFFu);
    b[9] = (uint8_t)((s->dwell >> 8) & 0xFFu);
    b[10] = s->boot_hold;
    crc = asset_crc32_bytes(0xFFFFFFFFu, b, 22) ^ 0xFFFFFFFFu;
    put32(b + 28, crc);

    flash_erase_4k(CFG_ADDR);
    *(volatile uint32_t *)0x40003000u = 0xAAAAu;
    flash_program(CFG_ADDR, b, 32);
    return 1;
}
/* ---------- CRC32（逐位，不查表） ---------- */
uint32_t asset_crc32_byte(uint32_t crc, uint8_t b)
{
    int i;

    crc ^= (uint32_t)b;
    for (i = 0; i < 8; i++)
        crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    return crc;
}

uint32_t asset_crc32_bytes(uint32_t crc, const uint8_t *p, int n)
{
    int i;

    for (i = 0; i < n; i++)
        crc = asset_crc32_byte(crc, p[i]);
    return crc;
}
