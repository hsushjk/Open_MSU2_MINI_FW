// 让端点缓冲区的读取不被编译器常量折叠
// 根因：ep0_buf/ep2_buf 由 USB 外设用 DMA 写入，C 代码里从没写过它们，
// GCC 在 -Os 下判定其值恒为 0，于是把 handle_setup 里的 switch 折成
// case 0x00，GET_DESCRIPTOR 分支整段被删（连 usb_desc_get 都不再引用）。
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');

if (s.includes('VOLATILE_READ')) {
    console.log('already patched');
    process.exit(0);
}

// 1. 读取宏
s = s.replace(
    'static uint8_t  rx_ring[512];',
    'static uint8_t  rx_ring[512];\n\n' +
    '/* 下面这些缓冲由外设 DMA 写，C 侧从没写过，必须用 volatile 读，\n' +
    ' * 否则 -Os 会把内容当成常量 0 折掉整个解析分支（VOLATILE_READ）。 */\n' +
    '#define VOLATILE_READ(buf) ((volatile const uint8_t *)(buf))'
);

// 2. handle_setup 从 volatile 指针解析
s = s.replace(
    '    const uint8_t *b = ep0_buf;',
    '    volatile const uint8_t *b = VOLATILE_READ(ep0_buf);'
);

// 3. OUT 分支从 ep2_buf 拷贝数据
s = s.replace(
    'rx_ring[rx_head] = ep2_buf[k];',
    'rx_ring[rx_head] = VOLATILE_READ(ep2_buf)[k];'
);

fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: endpoint buffers now read through volatile pointers');