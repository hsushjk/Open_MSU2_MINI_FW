// 用固定 16 色调色板替换每帧现算的 median cut 调色板
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/Open_MSU2_MINI_Console/msu2_mini.py';
let s = fs.readFileSync(P, 'utf8');
const crlf = s.includes('\r\n');
s = s.replace(/\r\n/g, '\n');

const marker = '    def _encode_indexed(self, img):';
const i = s.indexOf(marker);
if (i < 0) { console.error('marker not found'); process.exit(1); }
const j = s.indexOf('\n    def ', i + marker.length);
if (j < 0) { console.error('next def not found'); process.exit(1); }

const block = `    # 固定 16 色调色板：4 级灰 + 6 个基色 + 2 个深色。
    # 顺序与取值都不随画面变化，抖动图案因此逐帧稳定，静态区域不会闪烁。
    PALETTE16 = [
        (0, 0, 0), (64, 64, 64), (128, 128, 128), (255, 255, 255),
        (255, 0, 0), (0, 200, 0), (0, 0, 255), (255, 220, 0),
        (0, 220, 220), (220, 0, 220), (0, 90, 200), (150, 90, 40),
        (90, 40, 0), (0, 60, 0), (60, 0, 60), (30, 30, 30),
    ]

    def _palette_image(self):
        im = Image.new("P", (1, 1))
        flat = []
        for r, g, b in self.PALETTE16:
            flat += [r, g, b]
        im.putpalette(flat + [0] * (768 - len(flat)))
        return im

    def _palette_bytes(self):
        out = bytearray(32)
        for i, (r, g, b) in enumerate(self.PALETTE16):
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            out[2 * i] = (v >> 8) & 0xFF
            out[2 * i + 1] = v & 0xFF
        return bytes(out)

    def _encode_indexed(self, img):
        from PIL import Image as _Image

        dither = _Image.Dither.FLOYDSTEINBERG if self.dither else _Image.Dither.NONE
        q = img.convert("RGB").quantize(palette=self._palette_image(), dither=dither)
        px = q.tobytes()              # 一个像素一个字节，取值 0..15

        if px == self._last_indexed:
            return b""
        self._last_indexed = px

        n = len(px) // 2
        idx = bytearray(n)
        for k in range(n):
            idx[k] = ((px[2 * k] & 0x0F) << 4) | (px[2 * k + 1] & 0x0F)
        if len(px) & 1:
            idx.append((px[-1] & 0x0F) << 4)

        out = bytearray()
        out += MiniProtocol.lcd_palette(self._palette_bytes())
        for k in range(0, len(idx), 128):
            out += MiniProtocol.lcd_index_segment(idx[k:k + 128])
        out += MiniProtocol.lcd_raw_end()
        return bytes(out)
`;

s = s.slice(0, i) + block + s.slice(j + 1);
if (crlf) s = s.replace(/\n/g, '\r\n');
fs.writeFileSync(P, s, 'utf8');
console.log('encoder: fixed 16-colour palette in place');