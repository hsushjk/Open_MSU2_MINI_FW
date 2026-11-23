// 启用 SysTick 并在开机时自检：delay_ms(10) 前后各读一次计数
const fs = require('fs');
const base = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/';
let p = fs.readFileSync(base + 'main_proto.c', 'utf8');
let n = 0;
function rep(a, b) { if (!p.includes(a)) { console.log('MISS: ' + a.slice(0, 50)); return; } p = p.replace(a, b); n++; }

rep('static uint32_t systick_now(void)\n{\n    return *(volatile uint32_t *)0xE000F008u;\n}',
    'static uint32_t systick_now(void)\n{\n    return *(volatile uint32_t *)0xE000F008u;\n}\n\nstatic void systick_init(void)\n{\n    *(volatile uint32_t *)0xE000F010u = 0x00FFFFFFu;   /* CMP */\n    *(volatile uint32_t *)0xE000F000u = 0x00000009u;   /* STRE | STE */\n}');

rep('    usb_init();',
    '    usb_init();\n\n    /* SysTick 自检：10 ms 应当约 480 000 个计数 */\n    {\n        uint32_t a, b, d;\n        uint8_t c[6];\n        systick_init();\n        a = systick_now();\n        delay_ms(10);\n        b = systick_now();\n        d = (b - a) / 48000u;      /* 折算成毫秒，应当接近 10 */\n        c[0] = 0x77u;\n        c[1] = (uint8_t)(d & 0xFFu);\n        c[2] = (uint8_t)((d >> 8) & 0xFFu);\n        c[3] = 0;\n        c[4] = 0;\n        c[5] = 0xAAu;\n        usb_write(c, 6);\n    }');

fs.writeFileSync(base + 'main_proto.c', p);
console.log('systick: ' + n + ' replacements');