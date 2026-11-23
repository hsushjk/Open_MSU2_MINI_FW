// 给 build.ps1 增加 proto 变体
const fs = require('fs');
const q = String.fromCharCode(39);
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/build.ps1';
let s = fs.readFileSync(p, 'utf8');

if (s.includes(q + 'proto' + q + ' { ' + q + 'main_proto.c' + q + ' }')) {
    console.log('already patched');
    process.exit(0);
}

s = s.replace(
    q + 'host' + q + ' { ' + q + 'main_usbhost.c' + q + ' }',
    q + 'host' + q + ' { ' + q + 'main_usbhost.c' + q + ' } ' + q + 'proto' + q + ' { ' + q + 'main_proto.c' + q + ' }'
);
s = s.replace(
    q + 'host' + q + ' { ' + q + 'msu2_host_fw' + q + ' }',
    q + 'host' + q + ' { ' + q + 'msu2_host_fw' + q + ' } ' + q + 'proto' + q + ' { ' + q + 'msu2_proto_fw' + q + ' }'
);
s = s.replace(q + 'host' + q + ', ', q + 'host' + q + ', ' + q + 'proto' + q + ', ');

fs.writeFileSync(p, s, 'utf8');
console.log('build.ps1: proto variant added');