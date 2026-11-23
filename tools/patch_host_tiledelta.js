// 8 位索引色改成 16×16 分块增量：只发变化的块；并默认关闭抖动（网格感太强）
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/Open_MSU2_MINI_Console/msu2_mini.py';
let s = fs.readFileSync(P, 'utf8');
const crlf = s.includes('\r\n');
s = s.replace(/\r\n/g, '\n');

// 1. 默认关抖动
s = s.replace(/self\.dither = _os\.environ\.get\("MSU2_DITHER", "1"\) != "0"/,
              'self.dither = _os.environ.get("MSU2_DITHER", "0") != "0"');

// 2. 替换 8 位编码器为分块增量
const marker = '    def _encode_indexed(self, img):';
const i = s.indexOf(marker);
if (i < 0) { console.error('marker not found'); process.exit(1); }
const j = s.indexOf('\n    def ', i + marker.length);

const block = `    def _encode_indexed(self, img):
        """8 位索引 + 16×16 分块增量。

        整帧 12.8 KB 在 64 字节包的链路上要 60 ms 以上；桌面上真正变化的
        往往只有一小片，所以按块比对，只发变化的块。整块 16×16 = 256 字节。
        """
        import numpy as _np

        rgb = _np.asarray(img.convert("RGB"), dtype=_np.int16)
        h, w = rgb.shape[0], rgb.shape[1]
        if self.dither:
            bay = _np.array(self.BAYER8, dtype=_np.int16).reshape(8, 8)
            bay = (bay - 32).astype(_np.int16)
            th = _np.tile(bay, ((h + 7) // 8, (w + 7) // 8))[:h, :w]
            rgb = _np.clip(rgb + (th * 3)[..., None], 0, 255)

        lvl = (rgb * 6 // 256).astype(_np.uint8)
        idx = (lvl[..., 0] * 36 + lvl[..., 1] * 6 + lvl[..., 2]).astype(_np.uint8)

        tile = 16
        prev = self._last_indexed
        out = bytearray()
        changed = 0
        for ty in range(0, h, tile):
            for tx in range(0, w, tile):
                blk = idx[ty:ty + tile, tx:tx + tile]
                if prev is not None and prev.shape == idx.shape:
                    old = prev[ty:ty + tile, tx:tx + tile]
                    if _np.array_equal(blk, old):
                        continue
                changed += 1
                payload = blk.tobytes()
                out += MiniProtocol.lcd_set_xy(tx, ty)
                out += MiniProtocol.lcd_set_size(tx + blk.shape[1], ty + blk.shape[0])
                out += MiniProtocol.lcd_enter_direct_mode()
                for k in range(0, len(payload), 128):
                    out += MiniProtocol.lcd_index8_segment(payload[k:k + 128])
                out += MiniProtocol.lcd_raw_end()

        self._last_indexed = idx
        if changed == 0:
            return b""
        return bytes(out)
`;

s = s.slice(0, i) + block + s.slice(j + 1);
if (crlf) s = s.replace(/\n/g, '\r\n');
fs.writeFileSync(P, s, 'utf8');
console.log('host: 8-bit tile-delta encoder, dithering off by default');