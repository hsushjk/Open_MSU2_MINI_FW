// 8 位索引色 + 有序（Bayer）抖动：216 色、无调色板传输、静态区域不闪
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/Open_MSU2_MINI_Console/msu2_mini.py';
let s = fs.readFileSync(P, 'utf8');
const crlf = s.includes('\r\n');
s = s.replace(/\r\n/g, '\n');

// 1. 协议命令
if (!s.includes('def lcd_index8_segment')) {
    s = s.replace(
`    def lcd_index_segment(payload):`,
`    def lcd_index8_segment(payload):
        """8 位索引段（0x10）：一字节一个像素，固定 216 色立方调色板。"""
        n = len(payload)
        return MiniProtocol._frame(Cmd.LCD, Cmd.LCD_DRAW, 0x10,
                                   (n >> 8) & 0xFF, n & 0xFF, 0x00) + payload

    def lcd_index_segment(payload):`);
}

// 2. 编码器：8 位路径
const marker = '    def _encode_indexed(self, img):';
const i = s.indexOf(marker);
if (i < 0) { console.error('marker not found'); process.exit(1); }
const j = s.indexOf('\n    def ', i + marker.length);

const block = `    # 有序抖动的 Bayer 8×8 矩阵（0..1）
    BAYER8 = [
        0, 32, 8, 40, 2, 34, 10, 42,
        48, 16, 56, 24, 50, 18, 58, 26,
        12, 44, 4, 36, 14, 46, 6, 38,
        60, 28, 52, 20, 62, 30, 54, 22,
        3, 35, 11, 43, 1, 33, 9, 41,
        51, 19, 59, 27, 49, 17, 57, 25,
        15, 47, 7, 39, 13, 45, 5, 37,
        63, 31, 55, 23, 61, 29, 53, 21,
    ]

    def _encode_indexed(self, img):
        """8 位索引：R/G/B 各 6 级（51 步长）的立方，索引 = r*36 + g*6 + b。

        用有序抖动而不是误差扩散：Bayer 抖动只影响像素自身邻域，
        画面局部变化不会顺着扫描线扩散成整片闪烁。
        """
        import numpy as _np

        rgb = _np.asarray(img.convert("RGB"), dtype=_np.int16)
        h, w = rgb.shape[0], rgb.shape[1]
        bay = _np.array(self.BAYER8, dtype=_np.int16).reshape(8, 8)
        bay = (bay - 32).astype(_np.int16)          # -32..31
        th = _np.tile(bay, ((h + 7) // 8, (w + 7) // 8))[:h, :w]
        th = (th * 3).astype(_np.int16)             # 抖动幅度约 ±96，覆盖半级（51）

        v = _np.clip(rgb + th[..., None], 0, 255)
        lvl = (v * 6 // 256).astype(_np.uint8)      # 0..5
        idx = (lvl[..., 0] * 36 + lvl[..., 1] * 6 + lvl[..., 2]).astype(_np.uint8)
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

s = s.slice(0, i) + block + s.slice(j + 1);
if (crlf) s = s.replace(/\n/g, '\r\n');
fs.writeFileSync(P, s, 'utf8');
console.log('host: 8-bit indexed encoder with ordered dithering');