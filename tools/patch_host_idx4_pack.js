// 修正：索引必须两个像素打包成一个字节（PIL 的 tobytes 是一个像素一个字节）
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/Open_MSU2_MINI_Console/msu2_mini.py';
let s = fs.readFileSync(P, 'utf8');
const crlf = s.includes('\r\n');
s = s.replace(/\r\n/g, '\n');

const oldBlock = `        idx = q.tobytes()
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
        return bytes(out)`;

const newBlock = `        px = q.tobytes()              # 一个像素一个字节，取值 0..15
        key = bytes(palbytes) + px
        if key == self._last_indexed:
            return b""
        self._last_indexed = key
        self._last_raw = self._to_rgb565(img)

        n = len(px) // 2
        idx = bytearray(n)
        for k in range(n):
            idx[k] = ((px[2 * k] & 0x0F) << 4) | (px[2 * k + 1] & 0x0F)

        out = bytearray()
        out += MiniProtocol.lcd_palette(bytes(palbytes))
        for k in range(0, n, 128):
            out += MiniProtocol.lcd_index_segment(idx[k:k + 128])
        out += MiniProtocol.lcd_raw_end()
        return bytes(out)`;

if (!s.includes(oldBlock)) {
    console.error('indexed block not found');
    process.exit(1);
}
s = s.replace(oldBlock, newBlock);
if (crlf) s = s.replace(/\n/g, '\r\n');
fs.writeFileSync(P, s, 'utf8');
console.log('msu2_mini.py: indices packed two per byte (128-byte segments)');