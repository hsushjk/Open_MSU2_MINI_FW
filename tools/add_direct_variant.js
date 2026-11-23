// 给 build.ps1 增加 direct 变体
const fs = require('fs');
const p = 'build.ps1';
let s = fs.readFileSync(p, 'utf8');
const q = String.fromCharCode(39);

if (s.includes(q + 'direct' + q + ' { ' + q + 'main_usbdirect.c' + q + ' }')) {
    console.log('already patched');
    process.exit(0);
}

s = s.replace(
    q + 'host' + q + ' { ' + q + 'main_usbhost.c' + q + ' }',
    q + 'host' + q + ' { ' + q + 'main_usbhost.c' + q + ' } ' +
    q + 'direct' + q + ' { ' + q + 'main_usbdirect.c' + q + ' }'
);
s = s.replace(
    q + 'host' + q + ' { ' + q + 'msu2_host_fw' + q + ' }',
    q + 'host' + q + ' { ' + q + 'msu2_host_fw' + q + ' } ' +
    q + 'direct' + q + ' { ' + q + 'msu2_direct_fw' + q + ' }'
);
s = s.replace(q + q + 'host' + q + ',', q + q + 'host' + q + ',');
s = s.replace(q + 'host' + q + ',', q + 'host' + q + ',' + q + 'direct' + q + ',');

fs.writeFileSync(p, s, 'utf8');
console.log('build.ps1: direct variant added');