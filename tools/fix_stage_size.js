// 修正 stage 缓冲区的尺寸计算：stage 是 [2][512]，sizeof(stage) 是 1024
const fs = require('fs');
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
let s = fs.readFileSync(p, 'utf8');
const crlf = s.indexOf('\r\n') >= 0;
if (crlf) s = s.split('\r\n').join('\n');
let changed = 0;

const fixes = [
    ['uint32_t room = (uint32_t)sizeof(stage) - stage_len;',
     'uint32_t room = (uint32_t)sizeof(stage[0]) - stage_len;'],
    ['if (stage_len == sizeof(stage) || (raw_left == 0u && stage_len > 0u)) {',
     'if (stage_len == sizeof(stage[0]) || (raw_left == 0u && stage_len > 0u)) {'],
];
for (const [a, b] of fixes) {
    if (!s.includes(a)) { console.error('未找到: ' + a); process.exit(1); }
    s = s.replace(a, b);
    changed++;
}

// 断流保护：原始像素流迟迟收不满时放弃当前段，回到帧解析，避免永久失步
const anchor = `                    if (stage_len == sizeof(stage[0]) || (raw_left == 0u && stage_len > 0u)) {
                        lcd_raw_write(stage[stage_idx], stage_len);
                        stage_len = 0u;
                        stage_idx ^= 1u;
                    }
                    continue;`;
const replaced = `                    if (stage_len == sizeof(stage[0]) || (raw_left == 0u && stage_len > 0u)) {
                        lcd_raw_write(stage[stage_idx], stage_len);
                        stage_len = 0u;
                        stage_idx ^= 1u;
                    }
                    stall = 0u;
                    continue;`;
if (!s.includes(anchor)) { console.error('未找到 flush 代码块'); process.exit(1); }
s = s.replace(anchor, replaced);

// 声明 stall 计数并在主循环里做断流检测
const declAnchor = `    for (;;) {
        int n;
        static uint32_t quiet;
        static uint32_t hello_tick;`;
const declNew = `    for (;;) {
        int n;
        static uint32_t quiet;
        static uint32_t hello_tick;
        static uint32_t stall;`;
if (!s.includes(declAnchor)) { console.error('未找到主循环头'); process.exit(1); }
s = s.replace(declAnchor, declNew);

const tickAnchor = `            rx_len += n;
            quiet = 0u;
            hello_tick = 0u;`;
const tickNew = `            rx_len += n;
            quiet = 0u;
            hello_tick = 0u;
            stall = 0u;`;
if (!s.includes(tickAnchor)) { console.error('未找到收包尾部'); process.exit(1); }
s = s.replace(tickAnchor, tickNew);

const guardAnchor = `        if (rx_len > 0) {
            int pos = 0;`;
const guardNew = `        /* 原始像素流中途断掉时放弃本段：否则剩下的字会被当成像素，
         * 之后的帧头全部错位，表现为画面冻结且切插件也无法恢复。 */
        if (raw_left > 0u && rx_len == 0) {
            if (++stall > 4000u) {
                raw_left = 0u;
                stage_len = 0u;
                lcd_raw_idle();
                stall = 0u;
            }
        }

        if (rx_len > 0) {
            int pos = 0;`;
if (!s.includes(guardAnchor)) { console.error('未找到解析入口'); process.exit(1); }
s = s.replace(guardAnchor, guardNew);

if (crlf) s = s.split('\n').join('\r\n');
fs.writeFileSync(p, s, 'utf8');
console.log('main_proto.c: stage 尺寸修正 ' + changed + ' 处，断流保护已加');