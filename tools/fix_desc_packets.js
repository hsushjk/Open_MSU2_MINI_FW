// 按原厂做法改造描述符发送：整包 64 字节发送，长度按 wLength 而不是描述符实际长度
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');

// 1) IN 完成分支：按 wLength 继续发 64 字节整包
const inStart = s.indexOf('case TOKEN_IN:');
const inEnd = s.indexOf('case TOKEN_OUT:');
if (inStart < 0 || inEnd < 0) { console.error('IN branch not found'); process.exit(1); }

const newIn = [
    '    case TOKEN_IN:',
    '        usb_in_count++;',
    '        if (ep0_evt) {',
    '            /* 原厂做法：不按描述符实际长度截断，按 wLength 发整包 64 字节。',
    '             * 描述符后面紧邻的就是字符串表和填充，多读几个字节不影响枚举。 */',
    '            if ((setup.bRequest == 0x06) && (setup.bmRequestType & 0x60) == 0) {',
    '                uint16_t total = 0;',
    '                const uint8_t *p = usb_desc_get((uint8_t)(setup.wValue >> 8),',
    '                                                (uint8_t)setup.wValue, &total);',
    '                uint16_t n = 0;',
    '                if (p != 0 && desc_offset < setup.wLength) {',
    '                    n = (uint16_t)(setup.wLength - desc_offset);',
    '                    if (n > 64)',
    '                        n = 64;',
    '                    ep0_send(p + desc_offset, n);',
    '                    desc_offset += n;',
    '                    break;',
    '                }',
    '                ep0_send_zlp();',
    '                break;',
    '            }',
    '        } else if (ep == 3u) {',
    '            reply_pending = 0;',
    '        }',
    '        if (pending_addr != 0xFFu) {',
    '            USB->DEV_ADDR = (uint8_t)((USB->DEV_ADDR & 0x80u) | pending_addr);',
    '            pending_addr = 0xFFu;',
    '        }',
    '        break;',
    '',
    ''
].join('\n');

s = s.slice(0, inStart) + newIn + s.slice(inEnd);
fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: descriptor IN now sends full 64-byte packets by wLength');