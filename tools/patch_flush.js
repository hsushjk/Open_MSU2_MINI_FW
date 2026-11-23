const fs = require('fs');
const dir = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/';

/* 1. lcd.c: 增加 spi_flush()，并让 spi_byte 在写之前不必清接收标志 */
let s = fs.readFileSync(dir + 'lcd.c', 'utf8');
if (!s.includes('void spi_flush(void)')) {
    const anchor = 'void lcd_command(uint8_t cmd)';
    const i = s.indexOf(anchor);
    if (i < 0) throw new Error('lcd_command anchor missing');
    s = s.slice(0, i) +
        '/* 丢掉接收寄存器里残留的字节。\n' +
        ' * 面板只写不读，它的最后一笔传输会把对方移回来的字节留在 RXNE 里；\n' +
        ' * 如果接着读 flash 而不清掉，第一个读到的就是这笔陈货，整个事务错位一个字节。 */\n' +
        'void spi_flush(void)\n{\n    if (SPI1->STATR & 1u)\n        (void)SPI1->DATAR;\n}\n\n' +
        s.slice(i);
    fs.writeFileSync(dir + 'lcd.c', s);
    console.log('lcd.c: spi_flush added');
} else {
    console.log('lcd.c: spi_flush already present');
}

/* 2. fw.h: 声明 */
let h = fs.readFileSync(dir + 'fw.h', 'utf8');
if (!h.includes('spi_flush')) {
    h = h.replace('uint8_t spi_byte(uint8_t out);', 'uint8_t spi_byte(uint8_t out);\nvoid spi_flush(void);');
    fs.writeFileSync(dir + 'fw.h', h);
    console.log('fw.h: declared');
}

/* 3. flash_spi.c: 每次断言片选前先清接收寄存器 */
let f = fs.readFileSync(dir + 'flash_spi.c', 'utf8');
const old = `    if (level)
        GPIOB->BSHR = FLASH_CS;
    else
        GPIOB->BCR = FLASH_CS;`;
const neu = `    if (level)
        GPIOB->BSHR = FLASH_CS;
    else {
        spi_flush();
        GPIOB->BCR = FLASH_CS;
    }`;
if (f.includes(old)) {
    f = f.replace(old, neu);
    fs.writeFileSync(dir + 'flash_spi.c', f);
    console.log('flash_spi.c: flush on CS assert');
} else {
    console.log('flash_spi.c: pattern MISS');
}