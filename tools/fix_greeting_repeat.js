// 握手包每 400 ms 重发，直到主机回 MSNCN
const fs = require('fs');
const p = 'src/main_usbhost.c';
let s = fs.readFileSync(p, 'utf8');

s = s.replace(
    'static uint8_t  greeted;',
    'static uint8_t  host_ready;'
);

s = s.replace(
    '        if (cfg_selected != 0u && !greeted) {\n            usb_write(hello, 6);\n            greeted = 1u;\n        }',
    '        if (cfg_selected != 0u && !host_ready) {\n            usb_write(hello, 6);\n            delay_ms(400);\n        }'
);

s = s.replace(
    '                        lcd_fill(0, 0, 160, 80, 0x07E0);\n                        lcd_fill(152, 0, 8, 8, 0xFFFF);',
    '                        host_ready = 1u;\n                        lcd_fill(0, 0, 160, 80, 0x07E0);\n                        lcd_fill(152, 0, 8, 8, 0xFFFF);'
);

fs.writeFileSync(p, s, 'utf8');
console.log('main_usbhost.c: greeting repeats until MSNCN');