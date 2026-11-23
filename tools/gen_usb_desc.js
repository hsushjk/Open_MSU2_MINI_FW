/* 从原厂镜像里抽出 USB 描述符，生成 src/usb_desc.c
 * 用法：node tools/gen_usb_desc.js <image.bin>
 */
const fs = require('fs');

const img = process.argv[2] || '../work/image.bin';
const b = fs.readFileSync(img);
const arr = (a, n) => Array.from(b.slice(a, a + n));

const fmt = (name, bytes) => {
    const lines = [];
    for (let i = 0; i < bytes.length; i += 12) {
        lines.push('    ' + bytes.slice(i, i + 12).map(x => '0x' + x.toString(16).padStart(2, '0')).join(', ') + ',');
    }
    return 'static const uint8_t ' + name + '[' + bytes.length + '] = {\n' + lines.join('\n') + '\n};\n';
};

let out = '/* USB 描述符：取自原厂镜像 0x5CE0/0x5C74/0x5CF4/0x5D19，保障既有上位机的枚举行为一致 */\n';
out += '#include "usb_desc.h"\n\n';
out += fmt('usb_device_desc', arr(0x5ce0, 18)) + '\n';
out += fmt('usb_config_desc', arr(0x5c74, 0x6b)) + '\n';
out += fmt('usb_report_desc', arr(0x5cf4, 0x22)) + '\n';
out += fmt('usb_string_1', arr(0x5d19, 0x0e)) + '\n';
out += fmt('usb_string_2', arr(0x5d28, 0x12)) + '\n';
out += fmt('usb_string_3', arr(0x5d3c, 0x16)) + '\n';
out += 'static const uint8_t usb_string_langid[4] = { 0x04, 0x03, 0x09, 0x04 };\n\n';
out += `const uint8_t *usb_desc_get(uint8_t type, uint8_t index, uint16_t *len)
{
    switch (type) {
    case 1: *len = sizeof(usb_device_desc); return usb_device_desc;
    case 2: *len = sizeof(usb_config_desc); return usb_config_desc;
    case 3:
        switch (index) {
        case 0: *len = sizeof(usb_string_langid); return usb_string_langid;
        case 1: *len = sizeof(usb_string_1); return usb_string_1;
        case 2: *len = sizeof(usb_string_2); return usb_string_2;
        case 3: *len = sizeof(usb_string_3); return usb_string_3;
        default: break;
        }
        break;
    case 0x22: *len = sizeof(usb_report_desc); return usb_report_desc;
    default: break;
    }
    *len = 0;
    return 0;
}
`;

const header = `#ifndef USB_DESC_H
#define USB_DESC_H

#include <stdint.h>

/* type 与 USB 描述符类型编号一致；index 仅对字符串描述符有效 */
const uint8_t *usb_desc_get(uint8_t type, uint8_t index, uint16_t *len);

#endif
`;

fs.writeFileSync('src/usb_desc.c', out);
fs.writeFileSync('src/usb_desc.h', header);
console.log('src/usb_desc.c', fs.statSync('src/usb_desc.c').size, 'bytes');