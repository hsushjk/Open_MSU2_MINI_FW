// 把 ADC 接进协议固件：main_proto.c 里 usb_init() 之后调用 adc_init()，fw.h 里补声明
const fs = require('fs');
const dir = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/';

// 1) main_proto.c
let p = dir + 'main_proto.c';
let s = fs.readFileSync(p, 'utf8');
if (s.includes('adc_init();')) {
    console.log('main_proto.c: already has adc_init()');
} else {
    s = s.replace('    usb_init();\n', '    usb_init();\n    adc_init();\n');
    if (!s.includes('adc_init();')) s = s.replace('    usb_init();', '    usb_init();\n    adc_init();');
    fs.writeFileSync(p, s, 'utf8');
    console.log('main_proto.c: adc_init() inserted ->', s.includes('adc_init();'));
}

// 2) fw.h
p = dir + 'fw.h';
s = fs.readFileSync(p, 'utf8');
if (!s.includes('adc_read')) {
    s = s.replace('void lcd_init(void);', 'void lcd_init(void);\n\nvoid adc_init(void);\nuint16_t adc_read(uint8_t ch);');
    if (!s.includes('adc_read')) {
        // 兜底：追加到文件末尾
        s += '\nvoid adc_init(void);\nuint16_t adc_read(uint8_t ch);\n';
    }
    fs.writeFileSync(p, s, 'utf8');
    console.log('fw.h: adc decls added');
} else {
    console.log('fw.h: already declared');
}