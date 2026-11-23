// 修正窗口终点：lcd_set_window 的参数是闭区间终点，而协议里的 x1/y1 是排他终点
const fs = require('fs');
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/lcd.c';
let s = fs.readFileSync(p, 'utf8');
const crlf = s.indexOf('\r\n') >= 0;
if (crlf) s = s.split('\r\n').join('\n');

const oldText = `void lcd_stream_begin(int x, int y, int x2, int y2)
{
    lcd_set_window(x, y, x2, y2);
    dc_set(1);
    cs_set(0);
}`;
const newText = `void lcd_stream_begin(int x, int y, int x2, int y2)
{
    /* 协议里的 x2/y2 是排他终点（set_size 传的是宽高累加值），
     * lcd_set_window 要的是闭区间终点，所以各减一。原厂 op 7 也是这么写的：
     * lcd_draw_hline(x0, y0, x1 - 1, y1 - 1)。 */
    dma_finish();
    lcd_set_window(x, y, x2 - 1, y2 - 1);
    dc_set(1);
    cs_set(0);
}`;
if (!s.includes(oldText)) { console.error('lcd_stream_begin 未找到'); process.exit(1); }
s = s.replace(oldText, newText);

if (crlf) s = s.split('\n').join('\r\n');
fs.writeFileSync(p, s, 'utf8');
console.log('lcd.c: 窗口终点改为闭区间');