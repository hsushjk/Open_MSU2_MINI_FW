// proto: 持续画计数器，用来判断"收到多少段/多少帧"
const fs = require('fs');
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
let s = fs.readFileSync(p, 'utf8');
let n = 0;
function rep(a, b) { if (!s.includes(a)) { console.log('MISS: ' + a.slice(0, 50)); return; } s = s.replace(a, b); n++; }

// 段内计数：每帧开始时清零，每收到一段流数据加一
rep('static int connected;', 'static int connected;\nstatic volatile uint32_t seg_in_frame;\nstatic volatile uint32_t frame_cnt;');

// op 7 开始流式时清零
rep('        case 0x07:', '        case 0x07:\n            seg_in_frame = 0u;');

// op 8 计数
rep('        case 0x08:', '        case 0x08:\n            seg_in_frame++;');

// 主循环里每约 0.5 秒画一次计数器（不依赖空闲）
const anchor = '    for (;;) {';
const inject = `    for (;;) {
        {
            static uint32_t tick;
            static uint32_t blink;
            if (++tick >= 60000u) {
                uint32_t g = seg_in_frame > 100u ? 100u : seg_in_frame;
                uint32_t r = frame_cnt % 100u;
                tick = 0u;
                blink ^= 1u;
                lcd_fill(10, 0, 100, 5, 0x0000);
                lcd_fill(10, 0, (int)g, 5, 0x07E0);
                lcd_fill(10, 6, 100, 5, 0x0000);
                lcd_fill(10, 6, (int)r, 5, 0xF800);
                lcd_fill(120, 0, 8, 8, blink ? 0xFFFF : 0x0000);
            }
        }`;

if (s.includes(anchor)) { s = s.replace(anchor, inject); n++; } else { console.log('MISS: for(;;)'); }

// 每处理完一帧（op 8 且长度不是 256 视为尾段？）—— 简化：每 100 段算一帧
rep('            seg_in_frame++;', '            seg_in_frame++;\n            if (seg_in_frame == 100u) frame_cnt++;');

fs.writeFileSync(p, s);
console.log('counters: ' + n + ' replacements');