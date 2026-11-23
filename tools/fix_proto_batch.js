// proto: 批量处理帧（去掉逐帧 memmove），并加"调试状态回送"用于测吞吐
const fs = require('fs');
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
let s = fs.readFileSync(p, 'utf8');
let n = 0;
function rep(a, b) { if (!s.includes(a)) { console.log('MISS: ' + a.slice(0, 50)); return; } s = s.replace(a, b); n++; }

rep('static uint8_t seg[256];\nstatic uint8_t rx[512];',
    'static uint8_t seg[256];\nstatic uint8_t rx[1024];\nstatic uint8_t debug_on;\nstatic volatile uint32_t frame_done;');

// class 0 sub 0x5A 打开调试回送
rep('        if (sub == 0x4Du) {',
    '        if (sub == 0x5Au) {\n            debug_on = f[2];\n            return;\n        }\n        if (sub == 0x4Du) {');

// op 9 结束时回送帧计数
rep('            case 0x09:\n                lcd_raw_idle();\n                return;',
    '            case 0x09: {\n                uint8_t st[6];\n                lcd_raw_idle();\n                frame_done++;\n                if (debug_on) {\n                    st[0] = 0x55;\n                    st[1] = (uint8_t)(frame_done & 0xFFu);\n                    st[2] = (uint8_t)((frame_done >> 8) & 0xFFu);\n                    st[3] = 0;\n                    st[4] = 0;\n                    st[5] = 0xAA;\n                    usb_write(st, 6);\n                }\n                return;\n            }');

// 主循环：用下标处理，整批结束后只搬一次
rep('        while (rx_len >= 6) {\n            int k;\n            handle_frame(rx);\n            for (k = 6; k < rx_len; k++)\n                rx[k - 6] = rx[k];\n            rx_len -= 6;\n        }',
    '        if (rx_len >= 6) {\n            int pos = 0;\n            while (pos + 6 <= rx_len) {\n                handle_frame(rx + pos);\n                pos += 6;\n            }\n            if (pos > 0) {\n                int k;\n                for (k = pos; k < rx_len; k++)\n                    rx[k - pos] = rx[k];\n                rx_len -= pos;\n            }\n        }');

fs.writeFileSync(p, s);
console.log('batched: ' + n + ' replacements');