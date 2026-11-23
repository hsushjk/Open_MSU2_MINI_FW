// 给 main_usbstep.c 加上"上电等待 + 显式硬复位 + 二次初始化"，用于验证
// 冷启动时面板是否因为电源还没稳定而整体忽略了初始化序列。
const fs = require('fs');
const p = 'src/main_usbstep.c';
let s = fs.readFileSync(p, 'utf8');

if (s.includes('等待面板电源稳定')) {
    console.log('already patched');
    process.exit(0);
}

const anchor = `    board_clock_init();
    lcd_gpio_init();
    lcd_init();
`;
const replacement = `    board_clock_init();
    lcd_gpio_init();

    /* 等待面板电源稳定，再给一个足够长的硬复位脉冲，然后初始化两次。
     * 若冷启动黑屏是"面板还没准备好就收到了初始化序列"，这一版就能显示。 */
    delay_ms(600);
    GPIOA->BCR = 1u;   /* PA0 = 面板 RESET，拉低 */
    delay_ms(30);
    GPIOA->BSHR = 1u;  /* 释放 */
    delay_ms(150);
    lcd_init();
    delay_ms(50);
    lcd_init();
`;

if (!s.includes(anchor)) {
    console.error('anchor not found');
    process.exit(1);
}

s = s.replace(anchor, replacement);
fs.writeFileSync(p, s, 'utf8');
console.log('patched: power-up delay + hard reset + double init');