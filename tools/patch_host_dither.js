// 编码器：4 位索引默认开抖动（16 色下最有效的画质改善），可用环境变量关掉
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/Open_MSU2_MINI_Console/msu2_mini.py';
let s = fs.readFileSync(P, 'utf8');
const crlf = s.includes('\r\n');
s = s.replace(/\r\n/g, '\n');

if (!s.includes('self.dither = False')) { console.error('dither flag not found'); process.exit(1); }
s = s.replace(
`        self.color4 = True          # 4 位索引色 + 每帧调色板（12.8 KB/帧，半个帧的字节数）
        self.dither = False         # 抖动会让相邻帧差异变大，默认关`,
`        self.color4 = True          # 4 位索引色 + 每帧调色板（6.7 KB/帧）
        # 16 色下抖动是性价比最高的画质手段：不增加字节数，只改索引值。
        import os as _os
        self.dither = _os.environ.get("MSU2_DITHER", "1") != "0"`);

if (crlf) s = s.replace(/\n/g, '\r\n');
fs.writeFileSync(P, s, 'utf8');
console.log('encoder: dithering on by default (MSU2_DITHER=0 to disable)');