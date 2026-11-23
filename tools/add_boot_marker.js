// proto: 加开机标记与联机标记，避免"黑屏"无法区分"没跑"与"没数据"
const fs = require('fs');
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
let s = fs.readFileSync(p, 'utf8');
let n = 0;

function rep(from, to) {
    if (!s.includes(from)) { console.log('MISS: ' + from.slice(0, 50)); return; }
    s = s.replace(from, to); n++;
}

// 1) 开机标记：清屏之后立刻画
rep('    lcd_fill(0, 0, 160, 80, 0x0000);',
    '    lcd_fill(0, 0, 160, 80, 0x0000);\n' +
    '    lcd_fill(0, 0, 8, 80, 0xFFFF);      /* 左白边：固件在跑 */\n' +
    '    lcd_fill(0, 72, 160, 8, 0x4208);    /* 底部灰条：画布区起点 */');

// 2) 联机标记：握手成功时右上角点亮
rep('        connected = 1u;',
    '        connected = 1u;\n' +
    '        lcd_fill(152, 0, 8, 8, 0x07E0);   /* 右上绿点：已联机 */');

fs.writeFileSync(p, s);
console.log('boot/online markers: ' + n + ' replacements');