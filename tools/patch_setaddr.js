// 修 SET_ADDRESS 的时序：状态阶段结束后再切地址
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');

if (s.includes('pending_addr')) {
    console.log('already patched');
    process.exit(0);
}

// 1. 加一个待生效的地址变量
s = s.replace(
    'static uint8_t  cfg_selected;',
    'static uint8_t  cfg_selected;\nstatic uint8_t  pending_addr = 0xFFu;   /* 0xFF = 没有待生效的地址 */'
);

// 2. SET_ADDRESS 只记录，不动 DEV_ADDR
s = s.replace(
    `    case 0x05:  /* SET_ADDRESS */
        USB->DEV_ADDR = (uint8_t)(setup.wValue & 0x7fu);
        ep0_send_zlp();
        return;`,
    `    case 0x05:  /* SET_ADDRESS：先寄下，等状态阶段完成再切 */
        pending_addr = (uint8_t)(setup.wValue & 0x7fu);
        ep0_send_zlp();
        return;`
);

// 3. EP0 的 IN 完成后如果地址待生效就切过去
s = s.replace(
    `        } else if (ep == EP3) {
            reply_pending = 0;
        }`,
    `        } else if (ep == EP3) {
            reply_pending = 0;
        }
        if (pending_addr != 0xFFu) {
            USB->DEV_ADDR = pending_addr;
            pending_addr = 0xFFu;
        }`
);

fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: SET_ADDRESS deferred to status stage');