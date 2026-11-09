const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('web/app.js', 'utf8');
const start = source.indexOf('function writeRgb888AsRgb565LE(');
if (start < 0) throw new Error('RGB565LE encoder helper is missing');
const bodyStart = source.indexOf('{', start);
const bodyEnd = source.indexOf('\n}', bodyStart);
if (bodyStart < 0 || bodyEnd < 0) throw new Error('RGB565LE encoder helper is malformed');
const helper = source.slice(start, bodyEnd + 2);
const sandbox = {};
vm.runInNewContext(helper + '\nglobalThis.encode = writeRgb888AsRgb565LE;', sandbox);
const fontMatch = source.match(/const TFT_FONT1_GLYPHS='([0-9a-f]+)'/);
if (!fontMatch || fontMatch[1].length !== 2560) {
  throw new Error('Firmware GLCD font table must contain exactly 256 five-byte glyphs');
}
const font = fontMatch[1];
for (const [character, expected] of [
  [' ', '0000000000'],
  ['0', '3e5149453e'],
  ['A', '7c1211127c'],
  ['a', '2054547840'],
  [':', '0000140000'],
  ['?', '0201590906'],
]) {
  const actual = font.slice(character.charCodeAt(0) * 10, character.charCodeAt(0) * 10 + 10);
  if (actual !== expected) throw new Error('GLCD glyph mismatch for ' + JSON.stringify(character));
}
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
