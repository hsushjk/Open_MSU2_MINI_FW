// 固件：op 0x0D —— RLE 压缩段（flag=1 行程编码，flag=0 原样）
const fs = require('fs');
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
const raw = fs.readFileSync(p, 'utf8');
const crlf = raw.indexOf('\r\n') >= 0;
let s = raw.split('\r\n').join('\n');

if (s.indexOf('rle_feed') >= 0) {
    console.log('already patched');
    process.exit(0);
}

const anchor1 = 'static uint32_t stage_len;';
const repl1 = [
    'static uint32_t stage_len;',
    'static uint8_t rle_mode;                 /* 0=原样 1=行程编码 */',
    'static uint8_t rle_acc[3];',
    'static uint8_t rle_n;',
    '',
    'static void stage_flush(void)',
    '{',
    '    if (stage_len > 0u) {',
    '        lcd_raw_write(stage[stage_idx], stage_len);',
    '        stage_len = 0u;',
    '        stage_idx ^= 1u;',
    '    }',
    '}',
    '',
    '/* 行程编码：每三个字节一组 [像素数, 高字节, 低字节]，展开后进 stage。 */',
    'static void rle_feed(uint8_t b)',
    '{',
    '    rle_acc[rle_n++] = b;',
    '    if (rle_n < 3u)',
    '        return;',
    '    rle_n = 0u;',
    '    {',
    '        uint32_t cnt = rle_acc[0];',
    '        uint8_t hi = rle_acc[1];',
    '        uint8_t lo = rle_acc[2];',
    '',
    '        while (cnt-- > 0u) {',
    '            if (stage_len + 2u > (uint32_t)sizeof(stage[0]))',
    '                stage_flush();',
    '            stage[stage_idx][stage_len++] = hi;',
    '            stage[stage_idx][stage_len++] = lo;',
    '        }',
    '    }',
    '}'
].join('\n');

if (s.indexOf(anchor1) < 0) { console.log('anchor1 missing'); process.exit(1); }
s = s.split(anchor1).join(repl1);

const anchor2 = '            case 0x0Cu:';
const repl2 = [
    '            case 0x0Du:                          /* 压缩段：f[5]=1 行程编码 */',
    '                stage_flush();',
    '                rle_mode = f[5];',
    '                rle_n = 0u;',
    '                raw_left = (uint32_t)((f[3] << 8) | f[4]);',
    '                return;',
    '            case 0x0Cu:'
].join('\n');

if (s.indexOf(anchor2) < 0) { console.log('anchor2 missing'); process.exit(1); }
s = s.split(anchor2).join(repl2);

const oldBlock = [
    '                if (raw_left > 0u) {',
    '                    uint32_t take = (uint32_t)(rx_len - pos);',
    '                    uint32_t room = (uint32_t)sizeof(stage[0]) - stage_len;',
    '                    uint32_t i;',
    '',
    '                    if (take > raw_left)',
    '                        take = raw_left;',
    '                    if (take > room)',
    '                        take = room;',
    '                    for (i = 0; i < take; i++)',
    '                        stage[stage_idx][stage_len + i] = rx[pos + (int)i];',
    '',
    '                    stage_len += take;',
    '                    pos += (int)take;',
    '                    raw_left -= take;',
    '',
    '                    if (stage_len == sizeof(stage[0]) || (raw_left == 0u && stage_len > 0u)) {',
    '                        lcd_raw_write(stage[stage_idx], stage_len);',
    '                        stage_len = 0u;',
    '                        stage_idx ^= 1u;',
    '                    }',
    '                    stall = 0u;',
    '                    continue;',
    '                }'
].join('\n');

const newBlock = [
    '                if (raw_left > 0u) {',
    '                    uint32_t take = (uint32_t)(rx_len - pos);',
    '                    uint32_t i;',
    '',
    '                    if (take > raw_left)',
    '                        take = raw_left;',
    '',
    '                    if (rle_mode) {',
    '                        for (i = 0; i < take; i++)',
    '                            rle_feed((uint8_t)rx[pos + (int)i]);',
    '                        pos += (int)take;',
    '                        raw_left -= take;',
    '                        if (raw_left == 0u)',
    '                            stage_flush();',
    '                    } else {',
    '                        uint32_t room = (uint32_t)sizeof(stage[0]) - stage_len;',
    '',
    '                        if (take > room)',
    '                            take = room;',
    '                        for (i = 0; i < take; i++)',
    '                            stage[stage_idx][stage_len + i] = rx[pos + (int)i];',
    '',
    '                        stage_len += take;',
    '                        pos += (int)take;',
    '                        raw_left -= take;',
    '',
    '                        if (stage_len == sizeof(stage[0]) || (raw_left == 0u && stage_len > 0u))',
    '                            stage_flush();',
    '                    }',
    '                    stall = 0u;',
    '                    continue;',
    '                }'
].join('\n');

if (s.indexOf(oldBlock) < 0) { console.log('raw block missing'); process.exit(1); }
s = s.split(oldBlock).join(newBlock);

s = s.split('                raw_left = 0u;\n                stage_len = 0u;').join(
    '                raw_left = 0u;\n                stage_len = 0u;\n                rle_n = 0u;');

fs.writeFileSync(p, crlf ? s.split('\n').join('\r\n') : s, 'utf8');
console.log('firmware: op 0x0D RLE decode added');