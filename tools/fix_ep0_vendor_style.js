// 按原厂 vec_B8 的写法重写 EP0 装包与分包：
//   首包：长度 = min(描述符总长, wLength, 64)，装包后 UEP0_CTRL_H = 0x40（绝对写）
//   续包：长度 = 64，装包后 UEP0_CTRL_H ^= 0x40（原厂就是异或）
//   零长度包：只写长度 0，不动 UEP0_CTRL_H
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8').replace(/\r\n/g, '\n');

function replaceFn(name, body) {
    const re = new RegExp('static [a-z0-9_ ]+ ' + name + '\\([^)]*\\)\\s*\\{[\\s\\S]*?\\n\\}\n', 'i');
    if (!re.test(s)) { console.error('not found: ' + name); process.exit(1); }
    s = s.replace(re, body + '\n');
}

replaceFn('ep0_arm', `static void ep0_arm_ex(uint16_t len, int first)
{
    UEP0_LEN = len;
    if (first)
        UEP0_H = 0x40u;      /* 原厂首包：绝对写，TOG=1 / TX ACK / RX ACK */
    else
        UEP0_H = (uint8_t)(UEP0_H ^ 0x40u);   /* 原厂续包：异或翻转 */
}`);

replaceFn('ep0_send', `static void ep0_send_ex(const uint8_t *data, uint16_t len, int first)
{
    uint16_t i;
    for (i = 0; i < len && i < 64; i++)
        ep0_buf[i] = data[i];

    usb_tx_len = len;
    for (i = 0; i < 8u; i++)
        usb_tx_snap[i] = (i < len) ? ep0_buf[i] : 0u;

    ep0_arm_ex(len, first);
}`);

replaceFn('ep0_send_zlp', `static void ep0_send_zlp_ex(void)
{
    UEP0_LEN = 0;
}`);

// 分包长度
replaceFn('desc_chunk_len', `static uint16_t desc_chunk_len(uint16_t total, uint16_t offset)
{
    uint16_t n;
    if (offset >= total)
        return 0u;
    if (offset == 0u) {
        n = total;
        if (setup.wLength < n)
            n = setup.wLength;
        if (n > 64u)
            n = 64u;
        return n;
    }
    return 64u;
}`);

// 调用点改名并区分首包/续包
s = s.replace(/ep0_send\(/g, 'ep0_send_ex(');
s = s.replace(/ep0_send_ex\(([^;]*?), (\d+)\);/g, 'ep0_send_ex($1, $2, 1);');
s = s.replace(/ep0_send_ex\(p \+ desc_offset, n\);/g, 'ep0_send_ex(p + desc_offset, n, desc_offset != 0u);');
s = s.replace(/ep0_send_zlp\(\);/g, 'ep0_send_zlp_ex();');

fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: EP0 arming rewritten to match vendor (first packet absolute, continuation XOR, ZLP untouched)');