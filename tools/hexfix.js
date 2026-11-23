#!/usr/bin/env node
/* hexfix.js - 把 objcopy 生成的 Intel HEX 整理成 WCHISPStudio 能吃的样子。
 *
 * 两个坑：

   1. objcopy 遇到段间对齐空洞时会开新记录、跳过空洞，HEX 里就出现地址不连续。
      WCHISPStudio 对此比较敏感（表现为"hex 转 bin 失败"或刷进去不启动）。
      这里把 min..max 之间的空洞全部填 0xFF，保证地址连续。
   2. 行尾统一成 CRLF。
 *
 * 用法：
 *   node hexfix.js <in.hex> [out.hex] [out.bin]
 */
'use strict';

const fs = require('fs');
const path = require('path');

function fail(msg) {
    process.stderr.write('hexfix: ' + msg + '\n');
    process.exit(1);
}

function parseHex(text) {
    const bytes = new Map();
    let base = 0;
    let min = Infinity;
    let max = -Infinity;

    for (const raw of text.split(/\r?\n/)) {
        const line = raw.trim();
        if (!line) continue;
        if (line[0] !== ':') fail('not a hex record: ' + line.slice(0, 20));

        const buf = Buffer.from(line.slice(1), 'hex');
        if (buf.length < 5) fail('short record');
        const len = buf[0];
        const addr = (buf[1] << 8) | buf[2];
        const type = buf[3];
        const data = buf.subarray(4, 4 + len);

        let sum = 0;
        for (let i = 0; i < 4 + len; i++) sum = (sum + buf[i]) & 0xff;
        const crc = buf[4 + len];
        if (((0x100 - sum) & 0xff) !== crc) fail('bad checksum at address 0x' + addr.toString(16));

        if (type === 0x00) {
            for (let i = 0; i < len; i++) {
                const a = base + addr + i;
                bytes.set(a, data[i]);
                if (a < min) min = a;
                if (a > max) max = a;
            }
        } else if (type === 0x01) {
            break;
        } else if (type === 0x02) {
            base = ((data[0] << 8) | data[1]) << 4;
        } else if (type === 0x04) {
            base = ((data[0] << 8) | data[1]) << 16;
        }
        /* 03 / 05 忽略 */
    }

    if (min === Infinity) fail('no data records');
    return { bytes, min, max };
}

function record(addr, data) {
    const head = [data.length, (addr >> 8) & 0xff, addr & 0xff, 0x00];
    let sum = head.reduce((a, b) => a + b, 0) + data.reduce((a, b) => a + b, 0);
    const crc = (0x100 - (sum & 0xff)) & 0xff;
    const body = Buffer.concat([Buffer.from(head), Buffer.from(data), Buffer.from([crc])]);
    return ':' + body.toString('hex').toUpperCase();
}

function main() {
    const argv = process.argv.slice(2);
    if (argv.length < 1) fail('usage: node hexfix.js <in.hex> [out.hex] [out.bin]');

    const inFile = argv[0];
    const outHex = argv[1] || inFile.replace(/\.hex$/i, '') + '.hex';
    const outBin = argv[2] || null;

    const { bytes, min, max } = parseHex(fs.readFileSync(inFile, 'utf8'));
    const size = max - min + 1;

    const flat = Buffer.alloc(size, 0xFF);
    for (const [a, v] of bytes) flat[a - min] = v;

    const lines = [];
    for (let off = 0; off < size; off += 16) {
        const addr = min + off;
        /* 超过 64 KB 时补一条扩展地址记录 */
        if (off > 0 && addr % 0x10000 === 0) {
            const hi = (addr >>> 16) & 0xffff;
            lines.push(record(0, [hi >> 8, hi & 0xff]));
        }
        lines.push(record(addr & 0xffff, Array.from(flat.subarray(off, Math.min(off + 16, size)))));
    }
    lines.push(':00000001FF');

    fs.writeFileSync(outHex, lines.join('\r\n') + '\r\n', 'ascii');
    if (outBin) fs.writeFileSync(outBin, flat);

    process.stdout.write(
        'hexfix: 0x' + min.toString(16) + '-0x' + max.toString(16) +
        ' (' + size + ' bytes), ' + lines.length + ' records' +
        (outBin ? ', bin written' : '') + '\n'
    );
}

main();