// 给 build.ps1 增加 host 变体
const fs = require('fs');
const p = 'build.ps1';
let s = fs.readFileSync(p, 'utf8');
const q = String.fromCharCode(39);

if (s.includes(q + 'host' + q + ' { ' + q + 'main_usbhost.c' + q + ' }')) {
    console.log('already patched');
    process.exit(0);
}

s = s.replace(
    q + 'usbforce' + q + ' { ' + q + 'main_usbforce.c' + q + ' }',
    q + 'usbforce' + q + ' { ' + q + 'main_usbforce.c' + q + ' } ' +
    q + 'host' + q + ' { ' + q + 'main_usbhost.c' + q + ' }'
);
s = s.replace(
    q + 'usbforce' + q + ' { ' + q + 'msu2_usbforce_fw' + q + ' }',
    q + 'usbforce' + q + ' { ' + q + 'msu2_usbforce_fw' + q + ' } ' +
    q + 'host' + q + ' { ' + q + 'msu2_host_fw' + q + ' }'
);
s = s.replace(q + 'usbforce' + q + ',', q + 'usbforce' + q + ', ' + q + 'host' + q + ',');

fs.writeFileSync(p, s, 'utf8');
console.log('build.ps1: host variant added');