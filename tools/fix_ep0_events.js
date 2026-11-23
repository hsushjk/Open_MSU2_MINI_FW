// 修正 EP0 事件识别：INT_ST 低四位是事件相位，不是端点号
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');
let changed = 0;

function rep(from, to) {
    if (s.includes(from)) { s = s.split(from).join(to); changed++; }
}

rep('    ep  = (uint8_t)(int_st & 0x3fu);',
`    /* INT_ST 低四位是"事件相位"，不是纯端点号：
     *   0x30..0x33      EP0 SETUP
     *   0x20/0x21/0x24  EP0 IN
     *   0x40 / 0x00     EP0 OUT
     *   0x22 EP2 IN    0x23 EP3 IN    0x02 EP2 OUT    0x04 EP4 OUT
     * 老代码把 int_st & 0x3f 当端点号，SETUP(0x32) 被判成"非 EP0"，
     * handle_setup() 从来没被调用，主机只能重发请求。 */
    ep0_evt = (uint8_t)(((int_st & 0x30u) == 0x30u) ||
                        int_st == 0x20u || int_st == 0x21u || int_st == 0x24u ||
                        int_st == 0x40u || int_st == 0x00u);
    ep = (uint8_t)(int_st & 0x07u);`);

rep('    uint8_t  ep;', '    uint8_t  ep;\n    uint8_t  ep0_evt;');

rep('        if (ep == EP0)\n            handle_setup_ptr();', '        if (ep0_evt)\n            handle_setup_ptr();');
rep('        if (ep == EP0) {\n            /* ', '        if (ep0_evt) {\n            /* ');
rep('        } else if (ep == EP3) {', '        } else if (ep == 3u) {');
rep('        if (ep == EP2) {', '        if (ep == 2u) {');
rep('        } else if (ep == EP0) {\n            UEP0_LEN = 0;', '        } else if (ep0_evt) {\n            UEP0_LEN = 0;');

fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: ep0 identification fixed (' + changed + ' edits)');
const left = (s.match(/ep == EP0/g) || []).length;
console.log('remaining ep == EP0: ' + left);