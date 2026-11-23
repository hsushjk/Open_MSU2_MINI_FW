// Open 版握手：识别 OPMSU、回 OPEND，并固定走整段传输
const fs = require('fs');
const P = 'C:/Users/hsush/Desktop/MSU2_MINI/Open_MSU2_MINI_Console/msu2_mini.py';
let s = fs.readFileSync(P, 'utf8');

const old = `                idx = buf.find(b"\\x00MSN")
                if idx >= 0 and len(buf) >= idx + 6:
                    self.version = bytes(buf[idx+4:idx+6]).decode("ascii", "ignore")
                    self.raw_mode = (self.version == "02")
                    log.info("检测到 MSN 设备, 版本=%s%s", self.version,
                             "（整段传输）" if self.raw_mode else "")
                    self.write(b"\\x00MSNCN")`;

const neo = `                idx = buf.find(b"\\x00OPM")
                if idx >= 0 and len(buf) >= idx + 6:
                    self.version = "02"
                    self.raw_mode = True
                    log.info("检测到 OPMSU 设备（整段传输）")
                    self.write(b"\\x00OPEND")`;

if (!s.includes(old)) {
    console.error('handshake block not found');
    process.exit(1);
}
s = s.replace(old, neo);

// 顺手把模块头注释里的协议名对齐
s = s.replace('MSN01', 'OPMSU').replace('MSNCN', 'OPEND');

fs.writeFileSync(P, s, 'utf8');
console.log('open host: handshake switched to OPMSU/OPEND');