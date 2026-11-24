// Merge the resident bootloader (0x0000..) and the application (0x1000..) into
// one Intel HEX image, filling the gap with 0xFF.
//
//   node tools/merge_hex.js boot.hex app.hex out.hex
const fs = require('fs');

function parseHex(path) {
  const mem = new Map();
  let base = 0;
  for (const raw of fs.readFileSync(path, 'utf8').split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line[0] !== ':') continue;
    const bytes = Buffer.from(line.slice(1), 'hex');
    const len = bytes[0];
    const addr = (bytes[1] << 8) | bytes[2];
    const type = bytes[3];
    const data = bytes.subarray(4, 4 + len);
    if (type === 0x00) {
      for (let i = 0; i < len; i++) mem.set(base + addr + i, data[i]);
    } else if (type === 0x04) {
      base = ((data[0] << 8) | data[1]) << 16;
    } else if (type === 0x02) {
      base = ((data[0] << 8) | data[1]) << 4;
    } else if (type === 0x01) {
      break;
    }
  }
  return mem;
}

function hexLine(addr, data) {
  const bytes = [data.length, (addr >> 8) & 0xff, addr & 0xff, 0x00, ...data];
  let sum = 0;
  for (const b of bytes) sum = (sum + b) & 0xff;
  bytes.push((0x100 - sum) & 0xff);
  return ':' + Buffer.from(bytes).toString('hex').toUpperCase();
}

const [, , bootPath, appPath, outPath] = process.argv;
if (!bootPath || !appPath || !outPath) {
  console.error('usage: node merge_hex.js <boot.hex> <app.hex> <out.hex>');
  process.exit(2);
}

const mem = new Map();
for (const [k, v] of parseHex(bootPath)) mem.set(k, v);
for (const [k, v] of parseHex(appPath)) mem.set(k, v);

let lo = Infinity;
let hi = -Infinity;
for (const k of mem.keys()) {
  if (k < lo) lo = k;
  if (k > hi) hi = k;
}
if (!isFinite(lo)) {
  console.error('empty input');
  process.exit(1);
}

const lines = [];
for (let a = lo; a <= hi; a += 16) {
  const chunk = [];
  for (let i = 0; i < 16; i++) chunk.push(mem.has(a + i) ? mem.get(a + i) : 0xff);
  lines.push(hexLine(a & 0xffff, chunk));
}
lines.push(':00000001FF');
fs.writeFileSync(outPath, lines.join('\r\n') + '\r\n', 'ascii');

// sanity: report the two regions
const bootEnd = Math.max(...[...parseHex(bootPath).keys()]);
const appStart = Math.min(...[...parseHex(appPath).keys()]);
console.log(
  `merged ${lo.toString(16)}..${hi.toString(16)} (boot ends 0x${bootEnd.toString(16)}, app starts 0x${appStart.toString(16)}), ` +
    `${lines.length} records, ${(fs.statSync(outPath).size / 1024).toFixed(1)} KB`
);
if (bootEnd >= appStart) {
  console.error('ERROR: boot overlaps app');
  process.exit(1);
}