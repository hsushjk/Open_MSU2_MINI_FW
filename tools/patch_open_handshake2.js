// Open 版握手：识别 OPMSU、回 OPEND（按行替换，避免行尾差异）
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/Open_MSU2_MINI_Console/msu2_mini.py';
const lines = fs.readFileSync(P, 'utf8').split('\n');

let start = -1;
for (let i = 0; i < lines.length; i++) {
    if (lines[i].includes('buf.find(b"\\x00MSN")')) { start = i; break; }
}
if (start < 0) { console.error('find line not found'); process.exit(1); }

let end = -1;
for (let i = start; i < Math.min(start + 15, lines.length); i++) {
    if (lines[i].includes('self.write(b"\\x00MSNCN")')) { end = i; break; }
}
if (end < 0) { console.error('write line not found'); process.exit(1); }

const indent = ' '.repeat(16);
const neo = [
    indent + 'idx = buf.find(b"\\x00OPM")',
    indent + 'if idx >= 0 and len(buf) >= idx + 6:',
    indent + '    self.version = "02"',
    indent + '    self.raw_mode = True',
    indent + '    log.info("检测到 OPMSU 设备（整段传输）")',
    indent + '    self.write(b"\\x00OPEND")'
];

lines.splice(start, end - start + 1, ...neo);
fs.writeFileSync(P, lines.join('\n'), 'utf8');
console.log('open host: handshake replaced (lines ' + (start + 1) + '-' + (end + 1) + ')');