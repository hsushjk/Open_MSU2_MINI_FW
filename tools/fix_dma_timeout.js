// 给所有 DMA/BSY 等待加超时，避免一处卡死拖死整个固件；并在 DMA 后清掉接收寄存器残留
const fs = require('fs');
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/lcd.c';
let s = fs.readFileSync(p, 'utf8');
let n = 0;
function rep(a, b) { if (!s.includes(a)) { console.log('MISS: ' + a.slice(0, 60)); return; } s = s.replace(a, b); n++; }

const oldSend = `static void dma_send(const uint8_t *src, uint32_t len)
{
    DMA1_CH3->CFGR &= ~DMA_CFGR_EN;
    DMA1_CH3->MADDR = (uint32_t)src;
    DMA1_CH3->PADDR = (uint32_t)&SPI1->DATAR;
    DMA1_CH3->CNTR = len;
    DMA1_CH3->CFGR |= DMA_CFGR_EN;

    while ((DMA1->INTFR & DMA_TCIF3) == 0)
        ;
    DMA1->INTFCR = DMA_TCIF3;

    /* DMA 的传输完成只表示最后一个字节已经写进 DATAR，它可能还在移位寄存器里。
     * 此时拉高 CS 会把尾巴截掉，表现为每次刷新的末尾出现噪点或错行。 */
    while (SPI1->STATR & (1u << 7))
        ;
}`;

const newSend = `static int dma_wait_done(uint32_t len)
{
    uint32_t guard = len * 200u + 100000u;
    while ((DMA1->INTFR & DMA_TCIF3) == 0) {
        if (--guard == 0u) {
            DMA1_CH3->CFGR &= ~DMA_CFGR_EN;
            DMA1->INTFCR = DMA_TCIF3;
            return 0;
        }
    }
    DMA1->INTFCR = DMA_TCIF3;
    return 1;
}

static void dma_send(const uint8_t *src, uint32_t len)
{
    uint32_t guard;

    DMA1_CH3->CFGR &= ~DMA_CFGR_EN;
    DMA1_CH3->MADDR = (uint32_t)src;
    DMA1_CH3->PADDR = (uint32_t)&SPI1->DATAR;
    DMA1_CH3->CNTR = len;
    DMA1_CH3->CFGR |= DMA_CFGR_EN;

    (void)dma_wait_done(len);

    /* DMA 完成只表示最后一个字节写进了 DATAR，它可能还在移位寄存器里。
     * 超时保护：面板异常时不能让这里把主循环拖死。 */
    guard = 200000u;
    while ((SPI1->STATR & (1u << 7)) != 0u) {
        if (--guard == 0u)
            break;
    }

    /* 传输期间 SPI 也在收对方移回来的字节，留着会让下一次 spi_byte 读到陈货 */
    spi_flush();
}`;

rep(oldSend, newSend);
fs.writeFileSync(p, s);
console.log('dma timeouts: ' + n + ' replacements');