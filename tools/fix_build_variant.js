// One-shot patch: register the usbstage / usbstep variants in build.ps1.
const fs = require('fs');
const path = require('path');
const p = path.join(__dirname, '..', 'build.ps1');
let s = fs.readFileSync(p, 'utf8');
let changed = false;

function ensure(anchor, addition, marker) {
  if (s.includes(marker)) return;
  if (!s.includes(anchor)) {
    console.log('anchor missing for ' + marker);
    return;
  }
  s = s.replace(anchor, anchor + addition);
  changed = true;
  console.log('inserted ' + marker.trim());
}

// main source mapping
ensure("'ladder' { 'main_ladder.c' }", "\n    'usbstep' { 'main_usbstep.c' }", "'main_usbstep.c'");
// output tag
ensure("'ladder' { 'msu2_ladder_fw' }", " 'usbstep' { 'msu2_usbstep_fw' }", "'msu2_usbstep_fw'");
// validate set
ensure("'usbstage', 'all')", "", "'usbstep'");
if (!s.includes("'usbstep', 'all')")) {
  s = s.replace(/'usbstage', 'all'\)/, "'usbstage', 'usbstep', 'all')");
  changed = true;
  console.log('validate set updated');
}

if (changed) fs.writeFileSync(p, s, 'utf8');
else console.log('nothing to do');