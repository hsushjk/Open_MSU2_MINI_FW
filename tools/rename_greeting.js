// 把设备主动招呼与主机回执从 MSN01/MSNCN 改成 OPMSU/OPEND
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
let s = fs.readFileSync(P, 'utf8');

let n = 0;

// 1) 招呼字符串：00 'O' 'P' 'M' 'S' 'U'
const oldHello = 'static const uint8_t hello[6] = { 0x00, 0x4D, 0x53, 0x4E, 0x30, 0x32 };';
const newHello = 'static const uint8_t hello[6] = { 0x00, 0x4F, 0x50, 0x4D, 0x53, 0x55 };';
if (s.includes(oldHello)) { s = s.replace(oldHello, newHello); n++; }

// 2) 收到的回执 OPEND 的 sub 字节是 'O' = 0x4F
if (s.includes('if (sub == 0x4Du)')) {
    s = s.replace('if (sub == 0x4Du)', 'if (sub == 0x4Fu)');
    n++;
}

// 3) 文件头注释
s = s.replace('握手（设备发 MSN02，主机回 MSNCN）', '握手（设备发 OPMSU，主机回 OPEND）');

fs.writeFileSync(P, s, 'utf8');
console.log('main_proto.c: ' + n + ' replacements (OPMSU/OPEND)');