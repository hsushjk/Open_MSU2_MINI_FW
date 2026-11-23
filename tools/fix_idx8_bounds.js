// 8 位索引段的消费边界修复 + 空段保护 + DMA 超时自恢复
const fs = require('fs');
const D = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/';
let changed = [];

// ---- main_proto.c：mode 4 按可用字节消费，不能再越界读 ----
{
    const P = D + 'main_proto.c';
    let s = fs.readFileSync(P, 'utf8');
    const crlf = s.includes('\r\n');
    s = s.replace(/\r\n/g, '\n');
    const bad = `                    } else if (seg_mode == 4u) {
                        for (i = 0; i < raw_left; i++) {
                            idx8_feed((uint8_t)rx[pos + (int)i]);
                        }
                        raw_left = 0u;
                        stage_flush();
                    } else if (seg_mode == 1u) {`;
    const good = `                    } else if (seg_mode == 4u) {
                        /* 每个字节生成两个像素字节，先看段缓冲放得下几个 */
                        uint32_t room = ((uint32_t)sizeof(stage[0]) - stage_len) / 2u;

                        if (take > room)
                            take = room;
                        if (take > 0u) {
                            for (i = 0; i < take; i++)
                                idx8_feed((uint8_t)rx[pos + (int)i]);
                            pos += (int)take;
                            raw_left -= take;
                        }
                        if (stage_len + 2u > sizeof(stage[0]) || (raw_left == 0u && stage_len > 0u))
                            stage_flush();
                    } else if (seg_mode == 1u) {`;
    if (!s.includes(bad)) { console.error('mode4 block not found'); process.exit(1); }
    s = s.replace(bad, good);

    // 段长度越界视为失步：大于 2 KB 一律丢弃
    s = s.replace(
`            case 0x10u:                          /* 8 位索引段：一字节一个像素 */
                stage_flush();
                raw_left = ((uint32_t)f[3] << 8) | f[4];
                seg_mode = 4u;
                break;`,
`            case 0x10u:                          /* 8 位索引段：一字节一个像素 */
                stage_flush();
                raw_left = ((uint32_t)f[3] << 8) | f[4];
                seg_mode = 4u;
                if (raw_left == 0u || raw_left > 2048u) {   /* 失步保护 */
                    raw_left = 0u;
                    seg_mode = 0u;
                }
                break;`);

    if (crlf) s = s.replace(/\n/g, '\r\n');
    fs.writeFileSync(P, s, 'utf8');
    changed.push('main_proto.c: mode4 边界 + 段长保护');
}

// ---- lcd.c：空段直接返回；DMA 超时后复位通道而不是一直等 ----
{
    const P = D + 'lcd.c';
    let s = fs.readFileSync(P, 'utf8');
    const crlf = s.includes('\r\n');
    s = s.replace(/\r\n/g, '\n');

    s = s.replace(
`void lcd_raw_write(const uint8_t *p, uint32_t n)
{
    dma_finish();          /* 上一段收尾（通常早就传完了） */
    dma_start(p, n);       /* 这一段的传输与后续接收重叠 */`,
`void lcd_raw_write(const uint8_t *p, uint32_t n)
{
    dma_finish();          /* 上一段收尾（通常早就传完了） */
    if (n == 0u)
        return;
    dma_start(p, n);       /* 这一段的传输与后续接收重叠 */`);

    // dma_finish 超时后禁用通道并清标志，避免下一次继续空等
    s = s.replace(
`        if (DMA1->INTFR & DMA_TCIF3) break;`,
`        if (DMA1->INTFR & DMA_TCIF3) break;
        if (guard == 1u) {                 /* 超时：复位通道并清标志 */
            DMA1_CH3->CFGR &= ~DMA_CFGR_EN;
            DMA1->INTFCR = DMA_TCIF3;
        }`);

    if (crlf) s = s.replace(/\n/g, '\r\n');
    fs.writeFileSync(P, s, 'utf8');
    changed.push('lcd.c: 空段跳过 + DMA 超时复位通道');
}
console.log(changed.join('\n'));