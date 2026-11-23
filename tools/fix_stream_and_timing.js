// 1) 流式写不再逐段翻转片选  2) 用 SysTick 量整帧耗时并回送
const fs = require('fs');
const base = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/';

// --- lcd.c：lcd_raw_write 不再动 CS/DC，交给 lcd_stream_begin / lcd_raw_idle
let l = fs.readFileSync(base + 'lcd.c', 'utf8');
const oldRaw = `void lcd_raw_write(const uint8_t *p, uint32_t n)
{
    if (n == 0 || n > 0xFFFFu)
        return;
    cs_set(0);
    dc_set(1);
    dma_send(p, n);
    cs_set(1);
}`;
const newRaw = `void lcd_raw_write(const uint8_t *p, uint32_t n)
{
    if (n == 0 || n > 0xFFFFu)
        return;
    /* 片选由 lcd_stream_begin 拉低、lcd_raw_idle 拉高，中间整帧保持低电平，
     * 与原厂一致。逐段翻转片选会让面板把每段当成一次独立事务，既慢又出接缝。 */
    dma_send(p, n);
}`;
if (l.includes(oldRaw)) { l = l.replace(oldRaw, newRaw); fs.writeFileSync(base + 'lcd.c', l); console.log('lcd.c: raw write keeps CS'); }
else console.log('MISS lcd_raw_write');

// --- main_proto.c：加 SysTick 计时
let p = fs.readFileSync(base + 'main_proto.c', 'utf8');
let n = 0;
function rep(a, b) { if (!p.includes(a)) { console.log('MISS: ' + a.slice(0, 50)); return; } p = p.replace(a, b); n++; }

rep('static uint8_t debug_on;',
    'static uint8_t debug_on;\nstatic uint32_t t_frame_start;\nstatic volatile uint16_t last_frame_ms;\n\nstatic uint32_t systick_now(void)\n{\n    return *(volatile uint32_t *)0xE000F008u;\n}');

rep('            case 0x07:\n                lcd_stream_begin(x0, y0, x1, y1);',
    '            case 0x07:\n                t_frame_start = systick_now();\n                lcd_stream_begin(x0, y0, x1, y1);');

rep('                frame_done++;',
    '                frame_done++;\n                last_frame_ms = (uint16_t)((systick_now() - t_frame_start) / 48000u);');

rep('                    st[3] = 0;\n                    st[4] = 0;',
    '                    st[3] = (uint8_t)(last_frame_ms & 0xFFu);\n                    st[4] = (uint8_t)((last_frame_ms >> 8) & 0xFFu);');

fs.writeFileSync(base + 'main_proto.c', p);
console.log('main_proto.c: ' + n + ' replacements');