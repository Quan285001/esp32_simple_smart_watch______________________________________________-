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

const imageFunctionStart = source.indexOf('async function convertImageToRgb565(');
if (imageFunctionStart < 0) throw new Error('Browser image conversion function is missing');
const imageFunctionBrace = source.indexOf('{', imageFunctionStart);
const imageFunctionEnd = source.indexOf('\n}', imageFunctionBrace);
if (imageFunctionBrace < 0 || imageFunctionEnd < 0) {
  throw new Error('Browser image conversion function is malformed');
}
const imageFunction = source.slice(imageFunctionStart, imageFunctionEnd + 2);
const imageDraws = [];
const clipArcs = [];
const fakeImage = class {
  constructor() { this.naturalWidth = 10; this.naturalHeight = 10; }
  async decode() {}
};
const fakeImageDocument = {
  createElement(tag) {
    if (tag !== 'canvas') throw new Error('Unexpected DOM element: ' + tag);
    const canvas = { width: 0, height: 0 };
    let compositedBackground = false;
    canvas.getContext = () => ({
      fillStyle: '',
      fillRect() { compositedBackground = true; },
      save() {},
      beginPath() {},
      arc(...args) { clipArcs.push(args); },
      clip() {},
      restore() {},
      drawImage(...args) { imageDraws.push(args); },
      getImageData(x, y, width, height) {
        const pixels = new Uint8ClampedArray(width * height * 4);
        for (let i = 0; i < pixels.length; i += 4) {
          pixels[i] = 255;
          pixels[i + 1] = 165;
          pixels[i + 2] = 0;
          pixels[i + 3] = imageSandbox.transparentPixel && !compositedBackground && i === 0 ? 0 : 255;
        }
        return { data: pixels };
      },
    });
    return canvas;
  },
};
const imageSandbox = {
  state: { images: [{ id: 'source-image', data: 'data:image/mock;base64,AA==', name: 'orange.png' }] },
  Image: fakeImage,
  document: fakeImageDocument,
  writeRgb888AsRgb565LE: sandbox.encode,
};
vm.runInNewContext(imageFunction + '\nglobalThis.convert = convertImageToRgb565;', imageSandbox);
const cropObject = {
  id: 'image-object', assetId: 'source-image', name: 'crop-test', visible: true,
  transform: { x: -10, y: 100, width: 40, height: 30, zIndex: 0 },
};
const cropDesign = {
  canvas: { backgroundColor: '#000000' },
  objects: [cropObject],
};
imageSandbox.convert(cropObject, cropDesign).then((converted) => {
  if (converted.x !== 0 || converted.y !== 100 ||
      converted.width !== 30 || converted.height !== 30) {
    throw new Error('Image crop must use the visible display intersection: ' +
      JSON.stringify({ x: converted.x, y: converted.y,
        width: converted.width, height: converted.height }));
  }
  if (converted.bytes.length !== 30 * 30 * 2) {
    throw new Error('Cropped image byte length does not match RGB565 dimensions');
  }
  for (let i = 0; i < converted.bytes.length; i += 2) {
    if (converted.bytes[i] !== 0x20 || converted.bytes[i + 1] !== 0xfd) {
      throw new Error('Orange source pixels were not encoded as RGB565LE at offset ' + i);
    }
  }
  const circle = clipArcs.find(args => args[0] === 120 && args[1] === 20 && args[2] === 120);
  if (!circle) throw new Error('Image conversion did not clip to the circular watch aperture');
  const outputDraw = imageDraws[1];
  if (!outputDraw || outputDraw[1] !== -5 || outputDraw[2] !== 0 ||
      outputDraw[3] !== 30 || outputDraw[4] !== 30) {
    throw new Error('Cropped source image placement/aspect ratio is incorrect: ' +
      JSON.stringify(outputDraw));
  }
  // Alpha inspection must use the final 30x30 crop rather than a 240x240
  // proxy, and must reject only when transparent output overlaps a lower layer.
  imageSandbox.transparentPixel = true;
  const lowerLayer = {
    id: 'lower-layer', type: 'shape', visible: true,
    transform: { x: 0, y: 0, width: 240, height: 240, zIndex: 0 },
  };
  const overlayObject = {
    id: 'overlay-image', assetId: 'source-image', name: 'overlay-test', visible: true,
    transform: { x: -10, y: 100, width: 40, height: 30, zIndex: 0 },
  };
  let transparentOverlayRejected = false;
  try {
    await imageSandbox.convert(overlayObject, {
      canvas: { backgroundColor: '#000000' },
      objects: [lowerLayer, overlayObject],
    });
  } catch (error) {
    transparentOverlayRejected = String(error.message).includes('transparent overlay images');
  }
  if (!transparentOverlayRejected) {
    throw new Error('Transparent image pixels overlapping a lower layer must be rejected');
  }
  imageSandbox.transparentPixel = false;
  console.log('WFS browser image crop, alpha-overlap and orange RGB565LE pipeline tests passed.');
}).catch(error => {
  console.error(error);
  process.exitCode = 1;
});

