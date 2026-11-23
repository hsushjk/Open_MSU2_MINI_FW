// 生成 protorx：与 proto 相同，但 raw 段只计数、不写屏，用来分离"接收/环"与"画屏"的瓶颈
const fs = require('fs');
const base = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/';
let src = fs.readFileSync(base + 'src/main_proto.c', 'utf8');

// 1) 加一个只计数的 raw 写替代
src = src.replace(
    'static uint32_t raw_left;',
    'static uint32_t raw_left;\nstatic uint32_t raw_bytes_seen;'
);

// 2) 把 stage 冲刷处的 lcd_raw_write 换成计数
src = src.split('lcd_raw_write(stage, stage_len);').join('raw_bytes_seen += stage_len;');
src = src.split('lcd_raw_write(seg, (uint32_t)((f[3] << 8) | f[4]));').join('raw_bytes_seen += (uint32_t)((f[3] << 8) | f[4]);');

// 3) op9 状态包回传"本帧收到的字节数/256"
src = src.replace(
    'st[3] = (uint8_t)(usb_ep2_drop > 255u ? 255u : usb_ep2_drop);',
    'st[3] = (uint8_t)(raw_bytes_seen / 256u); raw_bytes_seen = 0u;'
);

fs.writeFileSync(base + 'src/main_protorx.c', src, 'utf8');
console.log('main_protorx.c written');

// 4) build.ps1 增加 protorx 变体
let b = fs.readFileSync(base + 'build.ps1', 'utf8');
if (!b.includes("'protorx'")) {
    b = b.replace("'proto' { 'main_proto.c' }", "'proto' { 'main_proto.c' } 'protorx' { 'main_protorx.c' }");
    b = b.replace("'proto' { 'msu2_proto_fw' }", "'proto' { 'msu2_proto_fw' } 'protorx' { 'msu2_protorx_fw' }");
    b = b.replace("'proto', ", "'proto', 'protorx', ");
    fs.writeFileSync(base + 'build.ps1', b, 'utf8');
    console.log('build.ps1: protorx added');
} else {
    console.log('build.ps1 already has protorx');
}
console.log('patched proto refs:', src.includes('raw_bytes_seen'));