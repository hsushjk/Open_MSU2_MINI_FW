// 投屏插件：两段式缩放（整数 reduce + LANCZOS）+ 可选锐化，提升小屏可读性
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/widgets/screen_cast4.py';
let s = fs.readFileSync(P, 'utf8');
const crlf = s.includes('\r\n');
s = s.replace(/\r\n/g, '\n');

if (s.includes('def _fit2')) { console.log('already patched'); process.exit(0); }

// 1. 新增两段式缩放函数
s = s.replace(
`def _fit(img, w, h, mode):`,
`def _fit2(img, w, h, mode, sharpen=True):
    """整数倍先降到接近目标，再 LANCZOS 缩到最终尺寸，最后轻度锐化。

    4K 桌面直接 BOX 缩到 160×80 会把文字糊成一团；先 reduce 掉大部分像素，
    再做一次高质量插值，细节保留明显更好。
    """
    iw, ih = img.size
    k = min(iw // max(1, w * 2), ih // max(1, h * 2))
    if k > 1:
        img = img.reduce(k)
        iw, ih = img.size

    if mode.startswith("拉伸"):
        out = img.resize((w, h), Image.LANCZOS)
    else:
        crop = mode.startswith("裁剪")
        scale = max(w / iw, h / ih) if crop else min(w / iw, h / ih)
        nw, nh = max(1, int(iw * scale + 0.5)), max(1, int(ih * scale + 0.5))
        img = img.resize((nw, nh), Image.LANCZOS)
        if crop:
            left = max(0, (nw - w) // 2)
            top = max(0, (nh - h) // 2)
            out = img.crop((left, top, left + w, top + h))
        else:
            out = Image.new("RGB", (w, h), (0, 0, 0))
            out.paste(img, ((w - nw) // 2, (h - nh) // 2))

    if sharpen:
        try:
            from PIL import ImageFilter
            out = out.filter(ImageFilter.UnsharpMask(radius=1.0, percent=70, threshold=2))
        except Exception:
            pass
    return out


def _fit(img, w, h, mode):`);

// 2. 抓帧线程改用 _fit2，并读取 sharpen 配置
s = s.replace(
`        return _fit(img, self.size[0], self.size[1], str(cfg.get("fit", "保持比例(裁剪)")))`,
`        return _fit2(img, self.size[0], self.size[1],
                     str(cfg.get("fit", "保持比例(裁剪)")),
                     bool(cfg.get("sharpen", True)))`);

// 3. 配置项加锐化开关
s = s.replace(
`        Field("show_fps", "bool", "显示 FPS", default=False),`,
`        Field("sharpen", "bool", "锐化（提升文字可读性）", default=True),
        Field("show_fps", "bool", "显示 FPS", default=False),`);

// 4. matches() 也要认 sharpen
s = s.replace(
`        keys = ("source", "monitor", "window_title", "fit")`,
`        keys = ("source", "monitor", "window_title", "fit", "sharpen")`);

if (crlf) s = s.replace(/\n/g, '\r\n');
fs.writeFileSync(P, s, 'utf8');
console.log('plugin: two-stage downscale + unsharp mask');