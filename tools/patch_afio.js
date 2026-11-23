// 一次性补丁：AFIO 的 USB 配置 + 枚举诊断计数器
const fs = require('fs');
const path = require('path');
const root = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware';

function edit(file, from, to) {
  const p = path.join(root, file);
  let t = fs.readFileSync(p, 'utf8');
  if (!t.includes(from)) { console.log('MISS ' + file + ': ' + from.slice(0, 40)); return false; }
  t = t.replace(from, to);
  fs.writeFileSync(p, t, 'utf8');
  console.log('ok   ' + file);
  return true;
}

// 1. regs.h: AFIO->CTLR
const regs = fs.readFileSync(path.join(root, 'src/regs.h'), 'utf8');
if (!regs.includes('AFIO_CTLR')) {
  fs.appendFileSync(path.join(root, 'src/regs.h'),
    '\n/* AFIO 控制寄存器：USB 需要在这里打开收发器 */\n' +
    '#define AFIO_CTLR (*(volatile uint32_t *)0x40010018u)\n', 'utf8');
  console.log('ok   regs.h');
}

// 2. usb.c: 计数器 + AFIO 写入
edit('src/usb.c',
  'int usb_irq_enable = 1;   /* 诊断用：置 0 则不开 NVIC */',
  'int usb_irq_enable = 1;   /* 诊断用：置 0 则不开 NVIC */\n\n' +
  '/* 枚举诊断：总线复位次数、收到的传输完成次数 */\n' +
  'volatile uint32_t usb_reset_count = 0;\n' +
  'volatile uint32_t usb_packet_count = 0;');

edit('src/usb.c',
  '    RCC->APB2PCENR |= RCC_APB2_USBFS;\n    RCC->AHBPCENR  |= RCC_AHB_USBFS;',
  '    RCC->APB2PCENR |= RCC_APB2_GPIOC | RCC_APB2_AFIO;\n' +
  '    RCC->AHBPCENR  |= RCC_AHB_USBFS;\n\n' +
  '    /* 原厂 usb_device_init 在复位脉冲之前写这一组位：\n' +
  '     * (CTLR & 0xffffff30) | 0x88 —— 打开 USB 收发器所需的 AFIO 配置。\n' +
  '     * 第一版漏了这一步，主机侧看不到 D+ 上拉，设备根本不会出现。 */\n' +
  '    AFIO_CTLR = (AFIO_CTLR & 0xffffff30u) | 0x88u;');

edit('src/usb.c',
  '    if (int_fg & INT_FG_BUS_RESET) {\n        USB->INT_FG = INT_FG_BUS_RESET;',
  '    if (int_fg & INT_FG_BUS_RESET) {\n        usb_reset_count++;\n        USB->INT_FG = INT_FG_BUS_RESET;');

// 3. main_usb.c: 画出两个计数器
edit('src/main_usb.c',
  '    for (;;) {\n        if (usb_take_greeting())',
  '    for (;;) {\n' +
  '        /* 诊断条：第一行绿格 = 总线复位次数，第二行青格 = 传输完成次数 */\n' +
  '        {\n' +
  '            uint32_t r = usb_reset_count > 20u ? 20u : usb_reset_count;\n' +
  '            uint32_t p = usb_packet_count > 20u ? 20u : usb_packet_count;\n' +
  '            static uint32_t last_r = 0xffffffffu, last_p = 0xffffffffu;\n' +
  '            static int inited = 0;\n' +
  '            if (!inited) {\n' +
  '                lcd_fill(0, 60, 80, 3, 0x0000);\n' +
  '                lcd_fill(0, 65, 80, 3, 0x0000);\n' +
  '                inited = 1;\n' +
  '            }\n' +
  '            if (r != last_r) { lcd_fill(0, 60, 80, 3, 0x0000); if (r) lcd_fill(0, 60, (int)(r * 4u), 3, 0x07E0); last_r = r; }\n' +
  '            if (p != last_p) { lcd_fill(0, 65, 80, 3, 0x0000); if (p) lcd_fill(0, 65, (int)(p * 4u), 3, 0x07FF); last_p = p; }\n' +
  '        }\n\n        if (usb_take_greeting())');