// 给 build.ps1 增加一个 usbregs 变体（ValidateSet + 主文件名 + 产物名三处）
const fs = require('fs');
const q = String.fromCharCode(39);
const p = 'build.ps1';
let s = fs.readFileSync(p, 'utf8');

if (s.includes(q + 'usbregs' + q + ' { ' + q + 'main_usbregs.c' + q + ' }')) {
    console.log('already patched');
    process.exit(0);
}

s = s.replace(
    q + 'usbstep' + q + ' { ' + q + 'main_usbstep.c' + q + ' }',
    q + 'usbstep' + q + ' { ' + q + 'main_usbstep.c' + q + ' } ' +
    q + 'usbregs' + q + ' { ' + q + 'main_usbregs.c' + q + ' }'
);

s = s.replace(
    q + 'usbstep' + q + ' { ' + q + 'msu2_usbstep_fw' + q + ' }',
    q + 'usbstep' + q + ' { ' + q + 'msu2_usbstep_fw' + q + ' } ' +
    q + 'usbregs' + q + ' { ' + q + 'msu2_usbregs_fw' + q + ' }'
);

s = s.replace(
    q + 'usbstep' + q + ', ',
    q + 'usbstep' + q + ', ' + q + 'usbregs' + q + ', '
);

fs.writeFileSync(p, s, 'utf8');
console.log('build.ps1: usbregs variant added');