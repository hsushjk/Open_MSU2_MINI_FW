// 给 Open 版上位机加 4 位索引色编码（0x0E 索引段 / 0x0F 调色板）
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/Open_MSU2_MINI_Console/msu2_mini.py';
let s = fs.readFileSync(P, 'utf8');
const crlf = s.includes('\r\n');
s = s.replace(/\r\n/g, '\n');

function must(find, repl, label) {
    if (!s.includes(find)) { console.error('NOT FOUND: ' + label); process.exit(1); }
    s = s.replace(find, repl);
    console.log('ok: ' + label);
}

// 1) 协议：调色板与索引段
must(
`    @staticmethod
    def lcd_raw_end():`,
`    @staticmethod
    def lcd_palette(pal_bytes):
        n = len(pal_bytes)
        return (MiniProtocol._frame(Cmd.LCD, Cmd.LCD_DRAW, 0x0F,
                                    (n >> 8) & 0xFF, n & 0xFF, 0x00)
                + bytes(pal_bytes))

    @staticmethod
    def lcd_index_segment(payload):
        n = len(payload)
        return (MiniProtocol._frame(Cmd.LCD, Cmd.LCD_DRAW, 0x0E,
                                    (n >> 8) & 0xFF, n & 0xFF, 0x00)
                + bytes(payload))

    @staticmethod
    def lcd_raw_end():`,
'protocol helpers');

// 2) 编码器状态
must(
`    def __init__(self):
        self._last_raw = None
        self._last_tile_raw = None

    def reset(self):
        self._last_raw = None
        self._last_tile_raw = None`,
`    def __init__(self):
        self._last_raw = None
        self._last_tile_raw = None
        self._last_indexed = None
        self.color4 = True          # 4 位索引色 + 每帧调色板（12.8 KB/帧，半个帧的字节数）
        self.dither = False         # 抖动会让相邻帧差异变大，默认关

    def reset(self):
        self._last_raw = None
        self._last_tile_raw = None
        self._last_indexed = None`,
'encoder state');

// 3) encode 分派
must(
`        if raw_mode:
            return bytes(out + self._encode_tiles(raw))`,
`        if raw_mode and self.color4:
            return bytes(out + self._encode_indexed(img))

        if raw_mode:
            return bytes(out + self._encode_tiles(raw))`,
'encode dispatch');

// 4) 索引色编码实现（放在 _encode_tiles 之前）
must(
`    TILE = 16
    TILE_FULL_THRESHOLD = 32`,
`    # ---- 4 位索引色 --------------------------------------------------
    #
    # 一帧 160x80 = 12800 像素。RGB565 是 25600 字节，索引色只要 6400 字节，
    # 加上每帧 32 字节调色板，整帧报文从 25.6 KB 降到约 13 KB。
    # 调色板用 PIL 的自适应量化（中位切分），16 色；解码在固件里做。
    def _encode_indexed(self, img):
        from PIL import Image as _Image

        method = _Image.MEDIANCUT
        dither = _Image.Dither.FLOYDSTEINBERG if self.dither else _Image.Dither.NONE
        q = img.convert("RGB").quantize(colors=16, method=method, dither=dither)

        pal = list(q.getpalette() or [])
        if len(pal) < 48:
            pal += [0] * (48 - len(pal))
        palbytes = bytearray(32)
        for i in range(16):
            r, g, b = pal[3 * i], pal[3 * i + 1], pal[3 * i + 2]
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            palbytes[2 * i] = (v >> 8) & 0xFF
            palbytes[2 * i + 1] = v & 0xFF

        idx = q.tobytes()
        key = bytes(palbytes) + idx
        if key == self._last_indexed:
            return b""
        self._last_indexed = key
        self._last_raw = self._to_rgb565(img)

        out = bytearray()
        out += MiniProtocol.lcd_palette(bytes(palbytes))
        for k in range(0, len(idx), 256):
            out += MiniProtocol.lcd_index_segment(idx[k:k + 256])
        out += MiniProtocol.lcd_raw_end()
        return bytes(out)

    TILE = 16
    TILE_FULL_THRESHOLD = 32`,
'indexed encoder');

if (crlf) s = s.replace(/\n/g, '\r\n');
fs.writeFileSync(P, s, 'utf8');
console.log('msu2_mini.py: 4-bit indexed encoding added');