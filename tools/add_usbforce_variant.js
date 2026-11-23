// 给 build.ps1 增加 usbforce 变体
const fs = require('fs');
const q = String.fromCharCode(39);
const p = 'build.ps1';
let s = fs.readFileSync(p, 'utf8');

if (s.includes(q + 'usbforce' + q + ' { ' + q + 'main_usbforce.c' + q + ' }')) {
    console.log('already patched');
    process.exit(0);
}

s = s.replace(
    q + 'usbregs' + q + ' { ' + q + 'main_usbregs.c' + q + ' }',
    q + 'usbregs' + q + ' { ' + q + 'main_usbregs.c' + q + ' } ' +
    q + 'usbforce' + q + ' { ' + q + 'main_usbforce.c' + q + ' }'
);
s = s.replace(
    q + 'usbregs' + q + ' { ' + q + 'msu2_usbregs_fw' + q + ' }',
    q + 'usbregs' + q + ' { ' + q + 'msu2_usbregs_fw' + q + ' } ' +
    q + 'usbforce' + q + ' { ' + q + 'msu2_usbforce_fw' + q + ' }'
);
s = s.replace(
    q + 'usbregs' + q + ', ',
    q + 'usbregs' + q + ', ' + q + 'usbforce' + q + ', '
);

fs.writeFileSync(p, s, 'utf8');
console.log('build.ps1: usbforce variant added');