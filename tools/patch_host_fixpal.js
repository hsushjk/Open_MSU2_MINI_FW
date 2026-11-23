// 固定调色板 + 抖动：消除静态区域的闪烁
// 原来每帧用 median cut 现算 16 色，只要画面里有一处变化（光标、秒数），
// 调色板顺序与取值就会轻微漂移，误差扩散抖动把这点漂移放大成整屏闪烁。
// 改成固定 16 色调色板后，同样的输入必然得到同样的索引，静态区域彻底稳定。
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/Open_MSU2_MINI_Console/msu2_mini.py';
let s = fs.readFileSync(P, 'utf8');
const crlf = s.includes('\r\n');
s = s.replace(/\r\n/g, '\n');

const oldStart = '    def _encode_indexed(self, img):';
const endMark = '    def encode(self, img, raw_mode=False):';
const iStart = s.indexOf(oldStart);
const iEnd = s.indexOf(endMark);
if (iStart < 0 || iEnd < 0 || iEnd < iStart) {
    console.error('encode_indexed block not found');
    process.exit(1);
}

const block = `    # 固定 16 色调色板：8 个灰阶 + 8 个基色。顺序固定，永远不随画面漂移。
    PALETTE16 = [
        (0, 0, 0), (85, 85, 85), (170, 170, 170), (255, 255, 255),
        (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0),
        (0, 255, 255), (255, 0, 255), (128, 0, 0), (0, 128, 0),
        (0, 0, 128), (128, 128, 0), (0, 128, 128), (128, 0, 128),
    ]

    def _palette_bytes(self):
        out = bytearray()
        for r, g, b in self.PALETTE16:
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            out += bytes(((v >> 8) & 0xFF, v & 0xFF))
        return bytes(out)

    def _palette_image(self):
        im = Image.new("P", (1, 1))
        flat = []
        for r, g, b in self.PALETTE16:
            flat += [r, g, b]
        im.putpalette(flat + [0] * (768 - len(flat)))
        return im

    def _encode_indexed(self, img):
        """固定调色板量化 + 误差扩散抖动，索引按两像素打包。"""
        rgb = img.convert("RGB")
        q = rgb.quantize(palette=self._palette_image(),
                         dither=Image.FLOYDSTEINBERG if self.dither else Image.NONE)
        px = q.tobytes()          # 一像素一字节，取值 0..15
        packed = bytearray((len(px) + 1) // 2)
        for k in range(0, len(px) - 1, 2):
            packed[k >> 1] = ((px[k] & 0x0F) << 4) | (px[k + 1] & 0x0F)
        if len(px) & 1:
            packed[len(px) >> 1] = ((px[-1] & 0x0F) << 4)
        return bytes(packed)

`;

s = s.slice(0, iStart) + block + s.slice(iEnd);
if (crlf) s = s.replace(/\n/g, '\r\n');
fs.writeFileSync(P, s, 'utf8');
console.log('encoder: fixed 16-colour palette + floyd-steinberg dither (stable indices)');