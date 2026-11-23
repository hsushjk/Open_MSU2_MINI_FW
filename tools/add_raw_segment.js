// 协议 v2：class 2 sub 3 op 0x0C —— 头 6 字节后直接跟像素数据
const fs = require('fs');
const base = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/';
let p = fs.readFileSync(base + 'main_proto.c', 'utf8');
let n = 0;
function rep(a, b) { if (!p.includes(a)) { console.log('MISS: ' + a.slice(0, 60)); return; } p = p.replace(a, b); n++; }

rep('static uint8_t debug_on;',
    'static uint8_t debug_on;\nstatic uint32_t raw_left;                 /* 还剩多少原始像素字节 */\nstatic uint8_t stage[512];\nstatic uint32_t stage_len;');

// op 0x0C：只记长度，数据由主循环直接喂给面板
rep('            case 0x0C:',
    '            case 0x0C:');
rep('            case 0x09: {',
    '            case 0x0Cu:\n                if (stage_len > 0u) { lcd_raw_write(stage, stage_len); stage_len = 0u; }\n                raw_left = (uint32_t)((f[3] << 8) | f[4]);\n                return;\n            case 0x09: {');

// op 9 之前把暂存区刷掉
rep('                lcd_raw_idle();\n                frame_done++;',
    '                if (stage_len > 0u) { lcd_raw_write(stage, stage_len); stage_len = 0u; }\n                lcd_raw_idle();\n                frame_done++;');

// 主循环：原始字节优先消费
const oldParse = `        if (rx_len >= 6) {
            int pos = 0;
            while (pos + 6 <= rx_len) {
                handle_frame(rx + pos);
                pos += 6;
            }
            if (pos > 0) {
                int k;
                for (k = pos; k < rx_len; k++)
                    rx[k - pos] = rx[k];
                rx_len -= pos;
            }
        }`;

const newParse = `        if (rx_len > 0) {
            int pos = 0;

            while (pos < rx_len) {
                if (raw_left > 0u) {
                    uint32_t take = (uint32_t)(rx_len - pos);
                    uint32_t room = (uint32_t)sizeof(stage) - stage_len;
                    uint32_t i;

                    if (take > raw_left)
                        take = raw_left;
                    if (take > room)
                        take = room;
                    for (i = 0; i < take; i++)
                        stage[stage_len + i] = rx[pos + (int)i];

                    stage_len += take;
                    pos += (int)take;
                    raw_left -= take;

                    if (stage_len == sizeof(stage) || (raw_left == 0u && stage_len > 0u)) {
                        lcd_raw_write(stage, stage_len);
                        stage_len = 0u;
                    }
                    continue;
                }

                if (rx_len - pos >= 6) {
                    handle_frame(rx + pos);
                    pos += 6;
                    continue;
                }
                break;
            }

            if (pos > 0) {
                int k;
                for (k = pos; k < rx_len; k++)
                    rx[k - pos] = rx[k];
                rx_len -= pos;
            }
        }`;

rep(oldParse, newParse);

fs.writeFileSync(base + 'main_proto.c', p);
console.log('proto v2 raw segment: ' + n + ' replacements');