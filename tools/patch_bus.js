const fs = require('fs');
const dir = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/';

/* 1. lcd.c：gpio_mode 定义在文件后半段，而 lcd_dc_as_* 插在它前面，
 *    补一条前置声明即可。 */
let s = fs.readFileSync(dir + 'lcd.c', 'utf8');
if (!s.includes('static void gpio_mode(GPIO_TypeDef *port, uint32_t pins, uint32_t nibble);')) {
    const anchor = 'uint8_t spi_byte(uint8_t out)';
    const i = s.indexOf(anchor);
    if (i < 0) throw new Error('spi_byte anchor missing');
    s = s.slice(0, i) +
        'static void gpio_mode(GPIO_TypeDef *port, uint32_t pins, uint32_t nibble);\n\n' +
        s.slice(i);
    fs.writeFileSync(dir + 'lcd.c', s);
    console.log('lcd.c: prototype added');
} else {
    console.log('lcd.c: prototype already present');
}

/* 2. flash_spi.c：所有读操作期间把 PA6 切成输入。 */
let f = fs.readFileSync(dir + 'flash_spi.c', 'utf8');
const pairs = [
    ['    uint8_t v;\n    cs_flash(0);', '    uint8_t v;\n    lcd_dc_as_input();\n    cs_flash(0);'],
    ['    v = spi_byte(0xFF);\n    cs_flash(1);\n    return v;', '    v = spi_byte(0xFF);\n    cs_flash(1);\n    lcd_dc_as_output();\n    return v;'],
    ['    int i;\n    cs_flash(0);', '    int i;\n    lcd_dc_as_input();\n    cs_flash(0);'],
    ['        dst[i] = spi_byte(0xFF);\n    cs_flash(1);', '        dst[i] = spi_byte(0xFF);\n    cs_flash(1);\n    lcd_dc_as_output();'],
    ['    cs_flash(0);\n    spi_byte(0x9F);', '    lcd_dc_as_input();\n    cs_flash(0);\n    spi_byte(0x9F);'],
    ['    id[2] = spi_byte(0xFF);\n    cs_flash(1);', '    id[2] = spi_byte(0xFF);\n    cs_flash(1);\n    lcd_dc_as_output();'],
];
let n = 0;
for (const [a, b] of pairs) {
    if (f.includes(a)) { f = f.split(a).join(b); n++; }
    else console.log('MISS:', JSON.stringify(a.split('\n')[0]));
}
fs.writeFileSync(dir + 'flash_spi.c', f);
console.log('flash_spi.c: ' + n + ' of ' + pairs.length + ' replacements');