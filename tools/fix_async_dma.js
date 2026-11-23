// 1) lcd.c：把 DMA 发送拆成"启动"与"收尾"，让上一段传输与接收重叠
// 2) main_proto.c：stage 改双缓冲，交替使用
const fs = require('fs');
const base = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/';
let lcd = fs.readFileSync(base + 'src/lcd.c', 'utf8');
let proto = fs.readFileSync(base + 'src/main_proto.c', 'utf8');
let fw = fs.readFileSync(base + 'src/fw.h', 'utf8');
let n = 0;

const oldStart = 'static void dma_send(const uint8_t *src, uint32_t len)';
if (lcd.includes(oldStart)) {
    const repl = `static volatile uint8_t dma_pending;

static void dma_start(const uint8_t *src, uint32_t len)
{
    DMA1_CH3->CFGR &= ~DMA_CFGR_EN;
    DMA1_CH3->MADDR = (uint32_t)src;
    DMA1_CH3->PADDR = (uint32_t)&SPI1->DATAR;
    DMA1_CH3->CNTR = len;
    DMA1_CH3->CFGR |= DMA_CFGR_EN;
    dma_pending = 1u;
}

static void dma_finish(void)
{
    uint32_t guard;

    if (!dma_pending)
        return;
    (void)dma_wait_done(0xFFFFu);
    guard = 200000u;
    while ((SPI1->STATR & (1u << 7)) != 0u) {
        if (--guard == 0u)
            break;
    }
    spi_flush();
    dma_pending = 0u;
}

static void dma_send(const uint8_t *src, uint32_t len)
{
    dma_finish();
    dma_start(src, len);
    dma_finish();
}`;
    lcd = lcd.replace(
        /static void dma_send\(const uint8_t \*src, uint32_t len\)\r?\n\{[\s\S]*?\n\}/,
        repl
    );
    n++;
}

// lcd_raw_write 改成异步：等上一笔收尾后立即启动下一笔，不等完成
lcd = lcd.replace(
    /void lcd_raw_write\(const uint8_t \*p, uint32_t n\)\r?\n\{[\s\S]*?\n\}/,
    `void lcd_raw_write(const uint8_t *p, uint32_t n)
{
    if (n == 0 || n > 0xFFFFu)
        return;
    dma_finish();          /* 上一段收尾（通常早就传完了） */
    dma_start(p, n);       /* 这一段的传输与后续接收重叠 */
}`
);

lcd = lcd.replace(
    /void lcd_raw_idle\(void\)\r?\n\{[\s\S]*?\n\}/,
    `void lcd_raw_idle(void)
{
    dma_finish();
    cs_set(1);
}`
);

// main_proto：双缓冲
if (proto.includes('static uint8_t stage[512];')) {
    proto = proto.replace(
        'static uint8_t stage[512];',
        'static uint8_t stage[2][512];\nstatic uint8_t stage_idx;'
    );
    proto = proto.replace(
        /stage\[stage_len \+ i\] = rx\[pos \+ \(int\)i\];/,
        'stage[stage_idx][stage_len + i] = rx[pos + (int)i];'
    );
    proto = proto.replace(
        /lcd_raw_write\(stage, stage_len\);\r?\n(\s*)stage_len = 0u;/g,
        'lcd_raw_write(stage[stage_idx], stage_len);\n$1stage_len = 0u;\n$1stage_idx ^= 1u;'
    );
    proto = proto.replace(
        /raw_bytes_seen \+= stage_len;/,
        'raw_bytes_seen += stage_len;'
    );
    n++;
}

fs.writeFileSync(base + 'src/lcd.c', lcd, 'utf8');
fs.writeFileSync(base + 'src/main_proto.c', proto, 'utf8');
console.log('async dma patch edits: ' + n);