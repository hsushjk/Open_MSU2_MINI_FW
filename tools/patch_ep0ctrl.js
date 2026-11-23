// EP0 控制字改成字节访问：低 16 位是长度，0x22 是状态字节
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');

if (s.includes('UEP0_H')) {
    console.log('already patched');
    process.exit(0);
}

// 宏定义插到 USB 宏之后
s = s.replace(
    '#define USB ((usb_regs_t *)0x40023400u)',
    '#define USB ((usb_regs_t *)0x40023400u)\n\n' +
    '/* EP0 控制寄存器按字节访问：0x20 起是 16 位长度，0x22 是状态字节。\n' +
    ' * 以前用 32 位写会把状态字节一起清掉/或保留旧的 NAK 值，\n' +
    ' * 导致控制传输的状态阶段一直被 NAK，主机等不到应答（代码 43）。 */\n' +
    '#define UEP0_LEN (*(volatile uint16_t *)0x40023420u)\n' +
    '#define UEP0_H   (*(volatile uint8_t  *)0x40023422u)'
);

// ep0_send / ep0_send_zlp / ep0_stall
s = s.replace(
    `    USB->UEP0_CTRL = len;
    USB->UEP0_CTRL ^= UEP_T_TOG << 16;   /* 翻转 TX 位 */`,
    `    UEP0_LEN = len;
    UEP0_H ^= 0x40u;                     /* 翻转 TX 位 */`
);

s = s.replace(
    `    USB->UEP0_CTRL = 0;
    USB->UEP0_CTRL ^= UEP_T_TOG << 16;`,
    `    UEP0_LEN = 0;
    UEP0_H ^= 0x40u;`
);

s = s.replace(
    `    USB->UEP0_CTRL = (USB->UEP0_CTRL & 0xffff0000u) | 0x0003u;`,
    `    UEP0_H = (uint8_t)((UEP0_H & 0xfcu) | 0x03u);   /* RX 状态 = STALL */`
);

// IN 完成之后清 RX 应答位（原厂做法：CTRL_H &= 0x0c）
s = s.replace(
    `            USB->UEP0_CTRL = (USB->UEP0_CTRL & 0xffff0000u) | UEP_R_ACK;
        } else if (ep == EP3) {`,
    `            UEP0_H &= 0x0cu;
        } else if (ep == EP3) {`
);

// OUT 状态阶段：原厂是 CTRL = 0 然后 CTRL_H = 0x40
s = s.replace(
    `        } else if (ep == EP0) {
            USB->UEP0_CTRL = (USB->UEP0_CTRL & 0xffff0000u) | UEP_R_ACK;
        }`,
    `        } else if (ep == EP0) {
            UEP0_LEN = 0;
            UEP0_H = 0x40u;
        }`
);

fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: EP0 control word now byte-accessed');