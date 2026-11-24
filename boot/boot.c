/* 常驻引导（0x0000 起，1 KB 对齐区内，永不擦除）
 *
 * 职责只有两件：
 *   1. 如果 NOR 里放着一份完整的新固件（头 + CRC 全对，且有“请更新”标志），
 *      就把它搬进 0x1000 起的应用区，逐页读回校验，成功后清掉标志；
 *   2. 跳到 0x1000 执行应用。
 *
 * 应用永远不会擦写 0x0000-0x0FFF，所以无论升级中途掉电、镜像写坏、
 * 应用崩溃，这块引导都还在，芯片永远能从 USB ISP 里救回来。
 *
 * 中断转发：本文件里的向量表把每个异常/中断转发到 RAM 里的地址表
 * （0x20000100，由应用启动时填入自己的处理函数），这样应用搬到 0x1000
 * 之后中断依然可用。
 */

typedef unsigned int u32;
typedef unsigned char u8;

#define REG(a) (*(volatile u32 *)(a))

#define RCC_APB2PCENR REG(0x40021018u)
#define RCC_APB2_GPIOA (1u << 2)
#define RCC_APB2_GPIOB (1u << 3)
#define RCC_APB2_SPI1 (1u << 12)

#define GPIOA_CFGLR REG(0x40010800u)
#define GPIOB_CFGLR REG(0x40010C00u)
#define GPIOB_BSHR REG(0x40010C10u)
#define GPIOB_BCR REG(0x40010C14u)

#define SPI1_CTLR1 REG(0x40013000u)
#define SPI1_CTLR2 REG(0x40013004u)
#define SPI1_STATR REG(0x40013008u)
#define SPI1_DATAR REG(0x4001300Cu)

#define FLASH_KEYR REG(0x40022004u)
#define FLASH_STATR REG(0x4002200Cu)
#define FLASH_CTLR REG(0x40022010u)
#define FLASH_ADDR REG(0x40022014u)
#define FLASH_MODEKEYR REG(0x40022024u)

#define IWDG_KR REG(0x40003000u)

#define NOR_IMG_ADDR 0x060000u
#define NOR_FLAG_ADDR 0x070000u
#define APP_BASE 0x1000u
#define APP_LIMIT 0xF000u /* 应用最大到 0xF000，后面留给 RAM 表以外的空间 */
#define PROBE_ADDR 0xF000u /* 最后一页用来体检，不碰应用区 */

static void iwdg(void)
{
    IWDG_KR = 0xAAAAu;
}

static void nor_cs(int level)
{
    if (level)
        GPIOB_BSHR = 0x080u;
    else
        GPIOB_BCR = 0x080u;
}

static u8 spi(u8 out)
{
    u32 guard = 0x20000u;

    while ((SPI1_STATR & 0x02u) == 0u && --guard)
        ;
    SPI1_DATAR = out;
    guard = 0x20000u;
    while ((SPI1_STATR & 0x01u) == 0u && --guard)
        ;
    return (u8)SPI1_DATAR;
}

static void nor_read(u32 addr, u8 *dst, u32 n)
{
    u32 i;

    nor_cs(0);
    spi(0x03);
    spi((u8)(addr >> 16));
    spi((u8)(addr >> 8));
    spi((u8)addr);
    for (i = 0; i < n; i++)
        dst[i] = spi(0xFF);
    nor_cs(1);
}

static void wait_bsy(void)
{
    u32 guard = 0x200000u;

    while ((FLASH_STATR & 1u) && --guard)
        ;
}

static void flash_unlock(void)
{
    FLASH_KEYR = 0x45670123u;
    FLASH_KEYR = 0xCDEF89ABu;
    FLASH_MODEKEYR = 0x45670123u;
    FLASH_MODEKEYR = 0xCDEF89ABu;
}

static void flash_lock(void)
{
    FLASH_CTLR |= (1u << 15);
    FLASH_CTLR |= (1u << 7);
}

static void flash_erase_page(u32 addr)
{
    FLASH_CTLR &= ~((1u << 5) | (1u << 17));
    FLASH_CTLR |= (1u << 1);
    FLASH_ADDR = addr;
    FLASH_CTLR |= (1u << 6);
    wait_bsy();
    FLASH_CTLR &= ~(1u << 1);
}

static void flash_program_page(u32 addr, const u8 *src)
{
    u32 i, w;

    FLASH_CTLR &= ~((1u << 5) | (1u << 17));
    FLASH_CTLR |= (1u << 16);
    FLASH_CTLR |= (1u << 19);
    wait_bsy();
    for (i = 0; i < 256u; i += 4u) {
        w = (u32)src[i] | ((u32)src[i + 1] << 8) | ((u32)src[i + 2] << 16) |
            ((u32)src[i + 3] << 24);
        *(volatile u32 *)(0x08000000u + addr + i) = w;
        FLASH_CTLR |= (1u << 18);
        wait_bsy();
    }
    FLASH_ADDR = 0x08000000u + addr;
    FLASH_CTLR |= (1u << 6);
    wait_bsy();
    FLASH_CTLR &= ~(1u << 16);
}

static void nor_erase_4k(u32 addr)
{
    nor_cs(0);
    spi(0x06);
    nor_cs(1);
    nor_cs(0);
    spi(0x20);
    spi((u8)(addr >> 16));
    spi((u8)(addr >> 8));
    spi((u8)addr);
    nor_cs(1);
    for (u32 i = 0; i < 200000u; i++)
        iwdg();
}

static u32 crc32_update(u32 crc, const u8 *p, u32 n)
{
    u32 i, k;

    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8u; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static void jump_app(void)
{
    void (*entry)(void) = (void (*)(void))APP_BASE;

    entry();
}

int boot_main(void)
{
    u8 hdr[16];
    u8 buf[256];
    u8 chk[256];
    u32 len, off, i;
    u32 crc = 0xFFFFFFFFu;
    int do_copy = 0;

    /* 引脚：PB7 = NOR 片选（推挽输出，空闲高）；PA5 = SCK、PA7 = MOSI（复用推挽）；
     *       PA6 兼 NOR 的 MISO 与屏的 D/C，这里先切成浮空输入。 */
    RCC_APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_SPI1;
    GPIOB_CFGLR = (GPIOB_CFGLR & ~(0xFu << 28)) | (0x1u << 28);
    GPIOA_CFGLR = (GPIOA_CFGLR & ~(0xFu << 20)) | (0x9u << 20);
    GPIOA_CFGLR = (GPIOA_CFGLR & ~(0xFu << 28)) | (0x9u << 28);
    GPIOA_CFGLR = (GPIOA_CFGLR & ~(0xFu << 24)) | (0x4u << 24);
    SPI1_CTLR1 = 0x0307u;
    SPI1_CTLR2 = 0x03u;
    SPI1_CTLR1 |= (1u << 6);
    GPIOB_BSHR = 0x080u;

    nor_read(NOR_IMG_ADDR, hdr, 16);
    nor_read(NOR_FLAG_ADDR, buf, 1);

    if (hdr[0] == 'M' && hdr[1] == 'S' && hdr[2] == 'U' && hdr[3] == '2' &&
        (hdr[4] & 0x01u) != 0u && buf[0] == 0xA5u) {
        len = (u32)hdr[6] | ((u32)hdr[7] << 8) | ((u32)hdr[8] << 16) |
              ((u32)hdr[9] << 24);
        if (len > 0u && len <= (APP_LIMIT - APP_BASE) && (len & 0xFFu) == 0u) {
            for (off = 0; off < len; off += 256u) {
                nor_read(NOR_IMG_ADDR + 16u + off, buf, 256u);
                crc = crc32_update(crc, buf, 256u);
                iwdg();
            }
            crc ^= 0xFFFFFFFFu;
            if (crc == ((u32)hdr[10] | ((u32)hdr[11] << 8) |
                        ((u32)hdr[12] << 16) | ((u32)hdr[13] << 24)))
                do_copy = 1;
        }
    }

    if (do_copy) {
        /* 先体检最后一页，确认这套寄存器序列在这颗片上真的能写 */
        u8 ok = 1;

        flash_unlock();
        flash_erase_page(PROBE_ADDR);
        for (i = 0; i < 256u; i++)
            buf[i] = (u8)(i ^ 0x5Au);
        flash_program_page(PROBE_ADDR, buf);
        for (i = 0; i < 256u; i++)
            if (*(volatile u8 *)(0x08000000u + PROBE_ADDR + i) != buf[i])
                ok = 0;
        flash_erase_page(PROBE_ADDR);
        iwdg();

        if (ok) {
            for (off = 0; off < len; off += 1024u)
                flash_erase_page(APP_BASE + off);
            for (off = 0; off < len; off += 256u) {
                nor_read(NOR_IMG_ADDR + 16u + off, buf, 256u);
                flash_program_page(APP_BASE + off, buf);
                for (i = 0; i < 256u; i++)
                    chk[i] = *(volatile u8 *)(0x08000000u + APP_BASE + off + i);
                for (i = 0; i < 256u; i++)
                    if (chk[i] != buf[i]) {
                        flash_lock();
                        jump_app(); /* 写坏了：保持引导，先跑旧应用 */
                    }
                iwdg();
            }
            flash_lock();
            nor_erase_4k(NOR_FLAG_ADDR); /* 清“请更新”标志 */
        } else {
            flash_lock();
        }
    }

    jump_app();
    return 0;
}