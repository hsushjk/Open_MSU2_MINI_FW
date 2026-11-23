// proto: 握手改成"串口安静时周期性打招呼"，避免设备误以为自己一直在线
const fs = require('fs');
const p = 'C:/Users/hsush/Desktop/MSU2_MINI/firmware/src/main_proto.c';
let s = fs.readFileSync(p, 'utf8');

const marker = '    for (;;) {';
const i = s.indexOf(marker);
if (i < 0) { console.log('MISS: loop'); process.exit(1); }

const tail = [
'    for (;;) {',
'        int n;',
'        static uint32_t quiet;',
'        static uint32_t hello_tick;',
'',
'        while (rx_len < (int)sizeof(rx)) {',
'            n = usb_read(rx + rx_len, (int)sizeof(rx) - rx_len);',
'            if (n <= 0)',
'                break;',
'            rx_len += n;',
'            quiet = 0u;',
'            hello_tick = 0u;',
'        }',
'',
'        while (rx_len >= 6) {',
'            int k;',
'            handle_frame(rx);',
'            for (k = 6; k < rx_len; k++)',
'                rx[k - 6] = rx[k];',
'            rx_len -= 6;',
'        }',
'',
'        /* 串口安静一段时间就周期性打招呼：主机的握手只在这时能被满足，',
'         * 一旦有数据进来立刻停，避免握手包混进图像流。 */',
'        if (++quiet >= 30000u) {',
'            if (hello_tick == 0u)',
'                usb_write(hello, 6);',
'            if (++hello_tick >= 150000u)',
'                hello_tick = 0u;',
'        }',
'    }',
'}',
''
].join('\n');

s = s.slice(0, i) + tail;
fs.writeFileSync(p, s);
console.log('main loop rewritten');