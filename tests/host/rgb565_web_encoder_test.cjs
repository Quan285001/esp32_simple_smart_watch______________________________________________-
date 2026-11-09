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
const glyphStart = source.indexOf('function setPreviewText(');
if (glyphStart < 0) throw new Error('Pixel-grid preview renderer is missing');
const glyphBrace = source.indexOf('{', glyphStart);
const glyphEnd = source.indexOf('\n}', glyphBrace);
if (glyphBrace < 0 || glyphEnd < 0) throw new Error('Pixel-grid preview renderer is malformed');
const glyphFunction = source.slice(glyphStart, glyphEnd + 2);
const fontDeclaration = source.match(/const TFT_FONT1_GLYPHS='[0-9a-f]+';/);
if (!fontDeclaration) throw new Error('Firmware GLCD font table is missing');
const drawCalls = [];
const fakeDocument = {
  createElement(tag) {
    if (tag !== 'canvas') return { style: {}, append() {} };
    return {
      style: {}, width: 0, height: 0,
      getContext() {
        return {
          imageSmoothingEnabled: true, fillStyle: '',
          fillRect(...args) { drawCalls.push(args); },
        };
      },
    };
  },
};
const glyphSandbox = {
  document: fakeDocument,
  TextEncoder,
  roundFirmwarePixel(value) { return Math.floor(Number(value) + 0.5); },
};
vm.runInNewContext(fontDeclaration[0] + '\n' + glyphFunction +
  '\nglobalThis.draw = setPreviewText;', glyphSandbox);
const preview = { append(bitmap) { this.bitmap = bitmap; } };
glyphSandbox.draw(preview, 'A',
  { fontSize: 8, textAlign: 'left', fill: '#fff', fontWeight: 400 },
  { width: 12, height: 8 });
if (preview.bitmap.width !== 12 || preview.bitmap.height !== 8) {
  throw new Error('Preview bitmap must use the device object pixel dimensions');
}
const expectedGlyphRuns = [
  [2, 0, 1, 1],
  [1, 1, 1, 1], [3, 1, 1, 1],
  [0, 2, 1, 1], [4, 2, 1, 1],
  [0, 3, 1, 1], [4, 3, 1, 1],
  [0, 4, 5, 1],
  [0, 5, 1, 1], [4, 5, 1, 1],
  [0, 6, 1, 1], [4, 6, 1, 1],
];
if (JSON.stringify(drawCalls) !== JSON.stringify(expectedGlyphRuns)) {
  throw new Error('GLCD A glyph rasterization differs from firmware pixel runs: ' +
    JSON.stringify(drawCalls));
}
drawCalls.length = 0;
const scaledPreview = { append(bitmap) { this.bitmap = bitmap; } };
glyphSandbox.draw(scaledPreview, 'A',
  { fontSize: 16, textAlign: 'center', fill: '#fff', fontWeight: 400 },
  { width: 24, height: 16 });
if (drawCalls[0]?.[0] !== 10 || drawCalls[0]?.[1] !== 0 ||
    drawCalls[0]?.[2] !== 2 || drawCalls[0]?.[3] !== 2) {
  throw new Error('Scaled and centered GLCD glyph does not follow firmware pixel rounding');
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
