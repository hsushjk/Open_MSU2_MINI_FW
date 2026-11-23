// 补回 usb_gpio_init 定义（PC16 = D-，PC17 = D+，含上拉）
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');

if (s.includes('void usb_gpio_init(void)\n{')) {
    console.log('usb_gpio_init already present');
    process.exit(0);
}

const body = [
    'void usb_gpio_init(void)',
    '{',
    '    /* PC16 = USB D- ：浮空输入；PC17 = USB D+ ：复用输入 + 上拉 */',
    '    gpio_mode(GPIOC, 0x00010000u, 0x4u);',
    '    gpio_mode(GPIOC, 0x00020000u, 0x8u);',
    '    GPIOC->BSXR |= (1u << 1);',
    '}',
    '',
    'void usb_init(void)'
].join('\n');

s = s.replace('void usb_init(void)', body);
fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: usb_gpio_init added');