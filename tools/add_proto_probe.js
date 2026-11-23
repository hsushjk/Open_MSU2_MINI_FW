// 加计数与"空闲 2 秒后画取证条"：不用额外固件就能知道设备到底处理到哪一段
const fs = require('fs');
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
let s = fs.readFileSync(p, 'utf8');

// 计数器
s = s.replace('static uint8_t seg[256];',
    'static uint8_t seg[256];\n\nstatic volatile uint32_t seg_cnt;   /* 已推送的段数 */\nstatic volatile uint32_t frame_cnt; /* 已处理的帧数 */');

// 段推送处计数
s = s.replace('            if (len)\n                lcd_raw_write(seg, len);',
    '            if (len) {\n                lcd_raw_write(seg, len);\n                seg_cnt++;\n            }');

// 每帧计数
s = s.replace('            handle_frame(rx);', '            handle_frame(rx);\n            frame_cnt++;');

// 主循环加空闲取证
s = s.replace(
    '        if (!connected) {',
    '        if (rx_len == 0) {\n' +
    '            if (++idle >= 400000u) {        /* 约 2 秒没有新数据 */\n' +
    '                uint32_t seg = seg_cnt > 100u ? 100u : seg_cnt;\n' +
    '                uint32_t fr = frame_cnt / 100u;\n' +
    '                if (fr > 100u) fr = 100u;\n' +
    '                lcd_fill(0, 0, 160, 6, 0x0000);\n' +
    '                lcd_fill(0, 8, (int)(seg * 16u) / 10u, 6, 0x07E0);\n' +
    '                lcd_fill(0, 16, (int)fr * 16u / 10u, 6, 0xF800);\n' +
    '                idle = 400001u;\n' +
    '            }\n' +
    '        } else {\n' +
    '            idle = 0u;\n' +
    '        }\n\n' +
    '        if (!connected) {'
);

fs.writeFileSync(p, s, 'utf8');
console.log('proto: counters + idle postmortem bars');