// 统计每帧实际写入的像素数，op9 时角上给结论：绿=12800 正确，红=数量不对
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
let s = fs.readFileSync(P, 'utf8');

if (s.includes('frame_px')) {
    console.log('already patched');
    process.exit(0);
}

// 1) 声明
s = s.replace('static void stage_flush(void)', 'static uint32_t frame_px;\n\nstatic void stage_flush(void)');

// 2) 在 stage_flush 里累加（只改第一处 lcd_raw_write）
const marker = 'lcd_raw_write(stage[stage_idx], stage_len);';
const k = s.indexOf(marker);
if (k < 0) { console.error('stage_flush body not found'); process.exit(1); }
s = s.slice(0, k) + 'frame_px += (uint32_t)(stage_len >> 1);\n        ' + s.slice(k);

// 3) op 0x09 收尾后画角标
const op9 = 'case 0x09:';
const j = s.indexOf(op9);
if (j < 0) { console.error('case 0x09 not found'); process.exit(1); }
const idle = 'lcd_raw_idle();';
const m = s.indexOf(idle, j);
if (m < 0) { console.error('lcd_raw_idle in case 0x09 not found'); process.exit(1); }
const after = m + idle.length;
const verdict = `
                lcd_fill(152, 72, 8, 8, (frame_px == 12800u) ? 0x07E0u : 0xF800u);
                frame_px = 0u;`;
s = s.slice(0, after) + verdict + s.slice(after);

fs.writeFileSync(P, s, 'utf8');
console.log('main_proto.c: per-frame pixel counter with corner verdict');