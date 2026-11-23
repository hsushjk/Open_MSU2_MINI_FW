// 描述符分包：单包（<=64）按整包 64 字节发；多包按描述符实际长度收尾
const fs = require('fs');
const p = 'src/usb.c';
let s = fs.readFileSync(p, 'utf8');

// 统一的取包长度规则，供 SETUP 与 IN 完成两处使用
const helper = [
    '/* 取下一包长度：单包描述符补足整包(64)，多包描述符按实际剩余长度收尾 */',
    'static uint16_t desc_chunk_len(uint16_t total, uint16_t offset)',
    '{',
    '    uint16_t limit = total;',
    '    uint16_t n;',
    '    if (limit < 64u)',
    '        limit = 64u;',
    '    if (setup.wLength < limit)',
    '        limit = setup.wLength;',
    '    if (offset >= limit)',
    '        return 0u;',
    '    n = (uint16_t)(limit - offset);',
    '    if (n > 64u)',
    '        n = 64u;',
    '    return n;',
    '}',
    ''
].join('\n');

if (!s.includes('desc_chunk_len')) {
    const anchor = 'static void ep0_send_zlp(void)';
    s = s.replace(anchor, helper + '\n' + anchor);
}

// SETUP 分支里的 GET_DESCRIPTOR
const setupOld = /n = total - desc_offset;[\s\S]*?if \(n > 64\)\s*n = 64;/;
if (setupOld.test(s)) {
    s = s.replace(setupOld, 'n = desc_chunk_len(total, desc_offset);');
}

// IN 完成分支里的 GET_DESCRIPTOR
const inOld = /n = \(uint16_t\)\(setup\.wLength - desc_offset\);[\s\S]*?if \(n > 64\)\s*n = 64;/;
if (inOld.test(s)) {
    s = s.replace(inOld, 'n = desc_chunk_len(total, desc_offset);');
}

fs.writeFileSync(p, s, 'utf8');
console.log('usb.c: descriptor chunking now matches vendor (single-packet descriptors padded to 64)');