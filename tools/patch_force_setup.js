// 强制 handle_setup 走真实调用：加 noinline 并经 volatile 函数指针调用，
// 防止 -Os 把它内联后整段消掉（现象：SETUP 分支只剩计数器自增）。
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');

if (s.includes('handle_setup_ptr')) {
    console.log('already patched');
    process.exit(0);
}

s = s.replace(
    'static void handle_setup(void)\n{',
    'static void handle_setup(void) __attribute__((noinline));\n' +
    'static void (* volatile handle_setup_ptr)(void) = handle_setup;\n\n' +
    'static void handle_setup(void)\n{'
);

s = s.replace(
    '        if (ep == EP0)\n            handle_setup();',
    '        if (ep == EP0)\n            handle_setup_ptr();'
);

fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: handle_setup now called through a volatile pointer');