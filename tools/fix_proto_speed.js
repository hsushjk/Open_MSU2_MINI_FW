// 提速：加大接收环、主循环不再固定延时、按帧边界消费
const fs = require('fs');
const dir = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/';

// 1) usb.c: 接收环 512 -> 4096
let u = fs.readFileSync(dir + 'usb.c', 'utf8');
u = u.replace('#define RX_RING_SIZE 512u', '#define RX_RING_SIZE 4096u');
fs.writeFileSync(dir + 'usb.c', u, 'utf8');

// 2) main_proto.c: 主循环改成背靠背收数据，不再每轮 delay_ms(5)
let m = fs.readFileSync(dir + 'main_proto.c', 'utf8');
const start = m.indexOf('int main(void)');
const head = m.substring(0, start);

const neu = [
    'int main(void)',
    '{',
    '    uint32_t idle = 0;',
    '',
    '    RCC->APB2PCENR |= RCC_APB2_GPIOA | RCC_APB2_GPIOB | RCC_APB2_GPIOC | RCC_APB2_AFIO;',
    '    board_clock_init();',
    '    lcd_gpio_init();',
    '    lcd_init();',
    '    lcd_fill(0, 0, 160, 80, 0x0000);',
    '',
    '    usb_init();',
    '',
    '    for (;;) {',
    '        int n;',
    '        int busy = 0;',
    '',
    '        while (rx_len < (int)sizeof(rx)) {',
    '            n = usb_read(rx + rx_len, (int)sizeof(rx) - rx_len);',
    '            if (n <= 0)',
    '                break;',
    '            rx_len += n;',
    '            busy = 1;',
    '        }',
    '',
    '        while (rx_len >= 6) {',
    '            int i;',
    '            handle_frame(rx);',
    '            for (i = 6; i < rx_len; i++)',
    '                rx[i - 6] = rx[i];',
    '            rx_len -= 6;',
    '        }',
    '',
    '        if (!connected) {',
    '            if (idle == 0u)',
    '                usb_write(hello, 6);',
    '            if (++idle >= 20000u)',
    '                idle = 0u;',
    '        }',
    '',
    '        (void)busy;',
    '    }',
    '}',
    ''
].join('\n');

fs.writeFileSync(dir + 'main_proto.c', head + neu, 'utf8');
console.log('proto main loop: no fixed delay, ring 4096');