// 1) 给 usb.c 的中断入口加 MIN_ISR 短路分支
// 2) 给 build.ps1 加 isrmin 变体（沿用 main_usbforce.c，加 -DMIN_ISR）
const fs = require('fs');
const q = String.fromCharCode(39);

let s = fs.readFileSync('src/usb.c', 'utf8');
if (!s.includes('MIN_ISR')) {
    s = s.replace(
        '{\n    usb_service();',
        '{\n#ifdef MIN_ISR\n' +
        '    /* 只清标志、计数，什么都不做：用来隔离"中断管线本身"与"处理逻辑" */\n' +
        '    USB->INT_FG = 0xFFu;\n' +
        '    usb_irq_count++;\n' +
        '    return;\n' +
        '#else\n' +
        '    usb_service();\n' +
        '#endif'
    );
    fs.writeFileSync('src/usb.c', s, 'utf8');
    console.log('usb.c: MIN_ISR branch added');
}

let b = fs.readFileSync('build.ps1', 'utf8');
if (!b.includes(q + 'isrmin' + q)) {
    b = b.replace(q + 'usbforce' + q + ',', q + 'usbforce' + q + ', ' + q + 'isrmin' + q + ',');
    b = b.replace(
        q + 'usbforce' + q + ' { ' + q + 'main_usbforce.c' + q + ' }',
        q + 'usbforce' + q + ' { ' + q + 'main_usbforce.c' + q + ' } ' +
        q + 'isrmin' + q + ' { ' + q + 'main_usbforce.c' + q + ' }'
    );
    b = b.replace(
        q + 'usbforce' + q + ' { ' + q + 'msu2_usbforce_fw' + q + ' }',
        q + 'usbforce' + q + ' { ' + q + 'msu2_usbforce_fw' + q + ' } ' +
        q + 'isrmin' + q + ' { ' + q + 'msu2_isrmin_fw' + q + ' }'
    );
    b = b.replace(
        "if ($Variant -eq 'csrtest' -or $Variant -eq 'min') { $cflags += '-DSTARTUP_NO_CSR' }",
        "if ($Variant -eq 'csrtest' -or $Variant -eq 'min') { $cflags += '-DSTARTUP_NO_CSR' }\n" +
        "if ($Variant -eq 'isrmin') { $cflags += '-DMIN_ISR' }"
    );
    fs.writeFileSync('build.ps1', b, 'utf8');
    console.log('build.ps1: isrmin variant added');
}