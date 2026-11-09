const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('web/app.js', 'utf8');
const match = source.match(/function writeRgb888AsRgb565LE\(red,green,blue,output,offset\)\s*\{[\s\S]*?\n\}/);
if (!match) throw new Error('RGB565LE encoder helper is missing');
const sandbox = {};
vm.runInNewContext(match[0] + '\nglobalThis.encode = writeRgb888AsRgb565LE;', sandbox);
const cases = [
  ['black', 0, 0, 0, 0x0000],
  ['white', 255, 255, 255, 0xffff],
  ['red', 255, 0, 0, 0xf800],
  ['green', 0, 255, 0, 0x07e0],
  ['blue', 0, 0, 255, 0x001f],
  ['orange', 255, 165, 0, 0xfd20],
  ['cyan', 0, 255, 255, 0x07ff],
];
for (const [name, r, g, b, expected] of cases) {
  const bytes = new Uint8Array(2);
  sandbox.encode(r, g, b, bytes, 0);
  const actual = bytes[0] | (bytes[1] << 8);
  if (actual !== expected) {
    throw new Error(name + ': expected 0x' + expected.toString(16) +
      ', got bytes [' + bytes[0].toString(16) + ',' + bytes[1].toString(16) + ']');
  }
}
console.log('WFS browser RGB888 -> RGB565LE golden colour tests passed.');
