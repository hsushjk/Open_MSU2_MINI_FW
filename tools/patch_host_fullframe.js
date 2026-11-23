// 8 位索引：默认整帧单窗口（画面整体更新、撕裂最小），分块增量改为可选（MSU2_TILES=1）
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/Open_MSU2_MINI_Console/msu2_mini.py';
let s = fs.readFileSync(P, 'utf8');
const crlf = s.includes('\r\n');
s = s.replace(/\r\n/g, '\n');

if (s.includes('_encode_indexed_tiles')) { console.log('already patched'); process.exit(0); }

// 1. 给现有分块编码器改名
s = s.replace('    def _encode_indexed(self, img):', '    def _encode_indexed_tiles(self, img):');

// 2. 插入整帧编码器与分派
const anchor = '    def _encode_indexed_tiles(self, img):';
const i = s.indexOf(anchor);
if (i < 0) { console.error('anchor missing'); process.exit(1); }

const full = `    def _quantize_index(self, img):
        """把 RGB 图量化成 216 色索引矩阵（R/G/B 各 6 级）。"""
        import numpy as _np
        rgb = _np.asarray(img.convert("RGB"), dtype=_np.int16)
        if self.dither:
            h, w = rgb.shape[0], rgb.shape[1]
            bay = _np.array(self.BAYER8, dtype=_np.int16).reshape(8, 8)
            bay = (bay - 32).astype(_np.int16)
            th = _np.tile(bay, ((h + 7) // 8, (w + 7) // 8))[:h, :w]
            rgb = _np.clip(rgb + (th * 3)[..., None], 0, 255)
        lvl = (rgb * 6 // 256).astype(_np.uint8)
        return (lvl[..., 0] * 36 + lvl[..., 1] * 6 + lvl[..., 2]).astype(_np.uint8)

    def _encode_indexed(self, img):
        """整帧单窗口：一次设窗口、一次收流，画面整体切换，撕裂最小。"""
        idx = self._quantize_index(img)
        payload = idx.tobytes()
        if payload == self._last_indexed:
            return b""
        self._last_indexed = payload

        out = bytearray()
        for k in range(0, len(payload), 128):
            out += MiniProtocol.lcd_index8_segment(payload[k:k + 128])
        out += MiniProtocol.lcd_raw_end()
        return bytes(out)

`;
s = s.slice(0, i) + full + s.slice(i);

// 3. 分派：走块还是走整帧
s = s.replace(
`        if raw_mode and self.color4:
            return bytes(out + self._encode_indexed(img))`,
`        if raw_mode and self.color4:
            import os as _os
            if _os.environ.get("MSU2_TILES", "0") == "1":
                return bytes(out + self._encode_indexed_tiles(img))
            return bytes(out + self._encode_indexed(img))`);

if (crlf) s = s.replace(/\n/g, '\r\n');
fs.writeFileSync(P, s, 'utf8');
console.log('host: full-frame indexed by default, tiles via MSU2_TILES=1');