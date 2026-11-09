const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { webcrypto } = require('node:crypto');
const { TextEncoder, TextDecoder } = require('node:util');
let fakeImageAlpha = 255;

class FakeElement {
  constructor(tagName) { this.tagName = tagName; this.attributes = {}; this.style = {}; this.children = []; }
  setAttribute(name, value) { this.attributes[name] = value; }
  append(child) { this.children.push(child); }
  getContext() {
    let fillStyle = '';
    return {
      set fillStyle(value) { fillStyle = value; },
      get fillStyle() { return fillStyle; },
      clearRect() {},
      fillRect() {},
      drawImage() {},
      getImageData(_x, _y, width, height) {
        const data = new Uint8ClampedArray(width * height * 4);
        for (let i = 0; i < data.length; i += 4) {
          data[i] = 255;
          data[i + 3] = fillStyle ? 255 : fakeImageAlpha;
        }
        return { data };
      },
    };
  }
}

class FakeImage {
  set src(value) { this.source = value; }
  decode() { return Promise.resolve(); }
}

const root = path.resolve(__dirname, '../..');
const appSource = fs.readFileSync(path.join(root, 'web/app.js'), 'utf8');
const compatibilitySource = fs.readFileSync(path.join(root, 'web/device-compatibility.js'), 'utf8');
const sandbox = {
  document: {
    querySelector: () => null,
    createElement: tagName => new FakeElement(tagName),
    createElementNS: (_namespace, tagName) => new FakeElement(tagName),
    querySelectorAll: () => [],
    addEventListener: () => {},
  },
  setTimeout,
  clearTimeout,
  crypto: webcrypto,
  Blob,
  TextEncoder,
  TextDecoder,
  structuredClone,
  Image: FakeImage,
};
vm.runInNewContext(compatibilitySource, sandbox, { filename: 'web/device-compatibility.js' });
vm.runInNewContext(
  appSource + '\n;globalThis.__wfsCodecTest = { state, encode: wfsEncodeFrame, decode: wfsDecodeFrame, waitFor: waitForWfsMessage, sendFrameAndWait: sendWfsFrameAndWait, buildPackage: buildWfsPackage, sendFrame: sendWfsFrame, renderShapePreview, renderAnalogClockPreview, roundFirmwarePixel, setPreviewText, renderWeatherPreview, crc32: wfsCrc32, hex: wfsHex };',
  sandbox,
  { filename: 'web/app.js' },
);

const { state, encode, decode, waitFor, sendFrameAndWait, buildPackage, sendFrame: wfsSendFrame, renderShapePreview, renderAnalogClockPreview, roundFirmwarePixel, setPreviewText, renderWeatherPreview, crc32, hex } = sandbox.__wfsCodecTest;
const writes = { control: [], data: [] };
state.wfs = { txSequence: 7, sessionId: 0x12345678, pending: new Map(), controlRx: { writeValueWithResponse: async frame => writes.control.push(frame) }, dataRx: { writeValueWithResponse: async frame => writes.data.push(frame) } };
state.schema = JSON.parse(fs.readFileSync(path.join(root, 'shared/schemas/watch-face.schema.json'), 'utf8'));

const payload = Uint8Array.from([0x10, 0x20, 0x30, 0x40]);
const frame = encode(0x30, payload);
const goldenHex = '5746010030000700785634120400df1f10203040';
assert.equal(Buffer.from(frame).toString('hex'), goldenHex,
  'Web encoder must match the byte-exact WFS v1 golden frame');

const decoded = decode(frame);
assert.equal(decoded.type, 0x30);
assert.equal(decoded.sequence, 7);
assert.equal(decoded.sessionId, 0x12345678);
assert.deepEqual(Array.from(decoded.payload), Array.from(payload));

const corrupted = frame.slice();
corrupted[6] ^= 1;
assert.throws(() => decode(corrupted), /CRC mismatch/);

(async () => {
  await assert.rejects(wfsSendFrame(0x10, new Uint8Array(221), 'control'),
    /control payload exceeds 220-byte limit/);
  await wfsSendFrame(0x22, new Uint8Array(224), 'data');
  assert.equal(writes.data[0].length, 240);
  await assert.rejects(wfsSendFrame(0x22, new Uint8Array(225), 'data'),
    /data payload exceeds 224-byte limit/);
  console.log('WFS Web BLE characteristic payload-boundary tests passed.');
})().catch(error => { console.error(error); process.exitCode = 1; });

(async () => {
  const originalWrite = state.wfs.controlRx.writeValueWithResponse;
  state.wfs.controlRx.writeValueWithResponse = async () => {
    throw Error('simulated GATT write failure');
  };
  await assert.rejects(
    sendFrameAndWait(0x05, new Uint8Array(), 0x06, 60000),
    /simulated GATT write failure/,
    'a failed GATT write must reject the request'
  );
  assert.equal(state.wfs.pending.size, 0,
    'a failed GATT write must cancel and remove its response waiter');
  state.wfs.controlRx.writeValueWithResponse = originalWrite;
  console.log('WFS Web failed-write waiter cleanup tests passed.');
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});

(async () => {
  const pendingAck = waitFor(0x23, 60000);
  assert.equal(state.wfs.pending.size, 1, 'ACK waiter should be registered');
  pendingAck.cancel();
  assert.equal(await pendingAck, null, 'cancelled ACK waiter should resolve without rejection');
  assert.equal(state.wfs.pending.size, 0, 'cancelled ACK waiter should be removed');

  const timedOut = waitFor(0x24, 5);
  await assert.rejects(timedOut, /WFS response timeout/);
  assert.equal(state.wfs.pending.size, 0, 'timed-out waiter should be removed');

  console.log('WFS Web protocol codec and waiter lifecycle tests passed.');
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});


(async () => {
  assert.equal(roundFirmwarePixel(1.5), 2);
  assert.equal(roundFirmwarePixel(-1.5), -2,
    'preview geometry must round negative half-pixels away from zero like lroundf');
  assert.equal(roundFirmwarePixel(2.49), 2);
  console.log('WFS Web-to-firmware pixel rounding tests passed.');
})().catch(error => { console.error(error); process.exitCode = 1; });

(async () => {
  const analogPreview = new FakeElement('div');
  renderAnalogClockPreview(analogPreview, { transform: { width: 100, height: 100 } }, new Date(2026, 0, 1, 3, 15, 30));
  const analogSvg = analogPreview.children[0];
  const analogLines = analogSvg.children.filter(node => node.tagName === 'line');
  assert.equal(analogLines.length, 15,
    'analog preview must include twelve ticks and three time-dependent hands');
  assert.ok(Number(analogLines[13].attributes.x2) > 80,
    'minute hand must follow the supplied time');
  assert.equal(analogLines[14].attributes.stroke, '#ff0000',
    'second hand must use the firmware red accent');
  assert.ok(Number(analogLines[14].attributes.y2) > 89,
    'second hand angle must follow the supplied seconds');
  console.log('WFS Web analog clock preview parity tests passed.');
})().catch(error => { console.error(error); process.exitCode = 1; });

(async () => {
  const textPreview = new FakeElement('div');
  setPreviewText(textPreview, '12:34', { scaleX: 1.2, scaleY: 0.8, textAlign: 'right' });
  assert.equal(textPreview.children[0].style.overflow, 'hidden',
    'text preview must clip glyphs to the configured object bounds');
  assert.equal(textPreview.children[0].style.justifyContent, 'flex-end',
    'text preview alignment must match the firmware text alignment');
  assert.equal(textPreview.children[0].children[0].textContent, '12:34');

  const weatherPreview = new FakeElement('div');
  renderWeatherPreview(weatherPreview, { weatherFields: ['temperature', 'condition'] },
    { fontSize: 40, scaleX: 1, scaleY: 1, textAlign: 'center' });
  assert.equal(weatherPreview.children.length, 2,
    'combined weather preview must use separate temperature and condition rows');
  assert.equal(weatherPreview.children[0].children[0].children[0].textContent, '27 C');
  assert.equal(weatherPreview.children[1].children[0].children[0].textContent, 'Cloudy');
  assert.match(weatherPreview.children[1].style.fontSize, /cqw$/,
    'condition preview must use the smaller firmware text size');

  const conditionOnly = new FakeElement('div');
  renderWeatherPreview(conditionOnly, { weatherFields: ['condition'] },
    { fontSize: 16, scaleX: 1, scaleY: 1, textAlign: 'center' });
  assert.equal(conditionOnly.children[0].children[0].textContent, 'Cloudy',
    'condition-only weather preview must not show temperature');
  console.log('WFS Web text clipping and weather preview parity tests passed.');
})().catch(error => { console.error(error); process.exitCode = 1; });

(async () => {
  const linePreview = new FakeElement('div');
  renderShapePreview(linePreview, { shape: 'line', transform: { width: 100, height: 50 } },
    { fill: '#ff0000', strokeWidth: 0 });
  const lineSvg = linePreview.children[0];
  assert.equal(lineSvg.tagName, 'svg');
  assert.equal(lineSvg.children[0].tagName, 'line');
  assert.equal(lineSvg.children[0].attributes.stroke, '#ff0000',
    'line preview must use the same fill-color fallback as firmware');
  assert.equal(lineSvg.children[0].attributes['stroke-width'], '1');

  const circlePreview = new FakeElement('div');
  renderShapePreview(circlePreview, { shape: 'circle', transform: { width: 80, height: 40 } },
    { fill: '#00ff00', strokeWidth: 0 });
  assert.equal(circlePreview.children[0].children[0].attributes.r, '20',
    'circle preview must use the smaller bounding-box dimension like firmware');

  const roundedPreview = new FakeElement('div');
  renderShapePreview(roundedPreview, { shape: 'rounded-rect', transform: { width: 60, height: 30 } },
    { fill: '#ffffff', cornerRadius: 7, strokeWidth: 0 });
  assert.equal(roundedPreview.children[0].children[0].attributes.rx, '7',
    'rounded-rectangle preview must use the configured firmware radius');
  console.log('WFS Web shape renderer parity tests passed.');
})().catch(error => { console.error(error); process.exitCode = 1; });

(async () => {
  const design = {
    schemaVersion: 1,
    designId: 'host_test_face',
    name: 'Host test face',
    deviceProfileId: 'esp32-c3-gc9a01-240',
    canvas: { width: 240, height: 240, shape: 'circle', backgroundColor: '#080b10', clipToShape: true },
    objects: [{
      id: 'clock_1',
      type: 'digital-clock',
      name: 'Time',
      visible: true,
      locked: false,
      transform: { x: 20, y: 90, width: 200, height: 40, anchor: 'top-left', zIndex: 0 },
      clockMode: 'digital',
      timeFormat: '24h',
      dataBinding: { source: 'time', format: 'HH:mm' },
      textStyle: { fontSize: 16, fontWeight: 400, fill: '#ffffff', scaleX: 1.4, scaleY: 0.8 },
    }],
  };
  state.wfsCapabilities = { upload: 1, obj: 16, json: 16384, stage: 32768, img: 1, el: 'text,digital-clock,analog-clock,date,weather,shape,image' };
  const packageResult = await buildPackage(design);
  const packageBytes = new Uint8Array(await packageResult.blob.arrayBuffer());
  const headerView = new DataView(packageBytes.buffer, packageBytes.byteOffset, 64);
  assert.deepEqual(Array.from(packageBytes.slice(0, 4)), [0x57, 0x46, 0x53, 0x31],
    'WFS package builder must emit the WFS1 magic');
  assert.equal(headerView.getUint16(4, true), 1);
  assert.equal(headerView.getUint16(6, true), 64);
  const manifestLength = headerView.getUint32(8, true);
  const layoutLength = headerView.getUint32(12, true);
  const payloadLength = headerView.getUint32(20, true);
  assert.equal(packageBytes.length, 64 + payloadLength);
  assert.equal(packageResult.packageLength, packageBytes.length);
  assert.equal(payloadLength, manifestLength + layoutLength);

  const payload = packageBytes.slice(64);
  const manifest = JSON.parse(new TextDecoder().decode(payload.slice(0, manifestLength)));
  const layout = JSON.parse(new TextDecoder().decode(payload.slice(manifestLength)));
  assert.equal(manifest.designId, design.designId);
  assert.deepEqual(manifest.requiredCapabilities, ['clock.digital']);
  assert.equal(layout.objects[0].textStyle.scaleX, 1.4);
  assert.equal(layout.objects[0].textStyle.scaleY, 0.8);

  const payloadDigest = new Uint8Array(await webcrypto.subtle.digest('SHA-256', payload));
  assert.equal(hex(payloadDigest), packageResult.payloadSha256);
  assert.equal(hex(payloadDigest), hex(packageBytes.slice(24, 56)));
  assert.equal(headerView.getUint32(56, true), crc32(packageBytes.slice(0, 56)));
  const layoutDigest = new Uint8Array(await webcrypto.subtle.digest(
    'SHA-256', payload.slice(manifestLength, manifestLength + layoutLength)));
  assert.equal(manifest.layoutSha256, hex(layoutDigest));

  const unsupportedWeatherDesign = structuredClone(design);
  unsupportedWeatherDesign.objects.push({
    id: 'wind_1',
    type: 'weather',
    name: 'Wind',
    transform: { x: 10, y: 10, width: 80, height: 20, anchor: 'top-left', zIndex: 1 },
    dataBinding: { source: 'temperature' },
    weatherFields: ['wind-speed'],
  });
  await assert.rejects(buildPackage(unsupportedWeatherDesign), /only temperature and condition weather fields/,
    'package builder must run shared device compatibility checks before packaging');

  const invalidDesign = structuredClone(design);
  invalidDesign.objects[0].textStyle.unknownVisualEffect = true;
  await assert.rejects(buildPackage(invalidDesign), /Schema validation failed/,
    'package builder must reject properties outside the shared schema');

  state.wfsCapabilities.stage = 64;
  await assert.rejects(buildPackage(design), /staging storage/,
    'package builder must reject a package larger than the reported staging capacity');

  const longPrefix = 'a'.repeat(50);
  state.images = [
    { id: longPrefix + '1', name: 'first.png', data: 'data:image/png;base64,AA==' },
    { id: longPrefix + '2', name: 'second.png', data: 'data:image/png;base64,BB==' },
  ];
  state.wfsCapabilities = { upload: 1, obj: 16, json: 16384, stage: 32768, img: 1, assets: 16,
    bw: 240, bh: 240, el: 'text,digital-clock,analog-clock,date,weather,shape,image' };
  const imageDesign = {
    schemaVersion: 1, designId: 'image_pipeline_test', name: 'Image pipeline test',
    deviceProfileId: 'esp32-c3-gc9a01-240',
    canvas: { width: 240, height: 240, shape: 'circle', backgroundColor: '#080b10', clipToShape: true },
    objects: [
      { id: 'image_one', type: 'image', name: 'Image one', assetId: longPrefix + '1', visible: true, locked: false,
        transform: { x: 0, y: 0, width: 2, height: 1, anchor: 'top-left', zIndex: 0 } },
      { id: 'image_two', type: 'image', name: 'Image two', assetId: longPrefix + '2', visible: true, locked: false,
        transform: { x: 2, y: 0, width: 2, height: 1, anchor: 'top-left', zIndex: 1 } },
    ],
  };
  fakeImageAlpha = 255;
  const imagePackage = await buildPackage(imageDesign);
  const imageBytes = new Uint8Array(await imagePackage.blob.arrayBuffer());
  const imageHeader = new DataView(imageBytes.buffer, imageBytes.byteOffset, 64);
  const imageManifestLength = imageHeader.getUint32(8, true);
  const imageLayoutLength = imageHeader.getUint32(12, true);
  const imagePayload = imageBytes.slice(64);
  const imageManifest = JSON.parse(new TextDecoder().decode(imagePayload.slice(0, imageManifestLength)));
  const imageLayout = JSON.parse(new TextDecoder().decode(imagePayload.slice(imageManifestLength, imageManifestLength + imageLayoutLength)));
  assert.equal(imageManifest.assets.length, 2);
  assert.notEqual(imageManifest.assets[0].assetId, imageManifest.assets[1].assetId,
    'truncated source IDs must not collide in the firmware package manifest');
  assert.deepEqual(Array.from(imagePayload.slice(imageManifestLength + imageLayoutLength, imageManifestLength + imageLayoutLength + 4)),
    [0, 248, 0, 248], 'image conversion must emit little-endian RGB565 pixels');
  assert.equal(imageLayout.objects[0].assetId, imageManifest.assets[0].assetId);
  assert.equal(imageLayout.objects[1].assetId, imageManifest.assets[1].assetId);

  fakeImageAlpha = 0;
  const transparentBottom = structuredClone(imageDesign);
  transparentBottom.objects = [transparentBottom.objects[0]];
  transparentBottom.objects[0].transform.zIndex = 5;
  await buildPackage(transparentBottom);
  const transparentImageWithDistantUnderlay = structuredClone(transparentBottom);
  transparentImageWithDistantUnderlay.objects.unshift({
    id: 'distant_underlay', type: 'shape', name: 'Distant underlay', shape: 'rect',
    transform: { x: 100, y: 100, width: 10, height: 10, anchor: 'top-left', zIndex: -1 },
    style: { fill: '#ffffff' },
  });
  await buildPackage(transparentImageWithDistantUnderlay);
  const transparentOverlay = structuredClone(transparentBottom);
  transparentOverlay.objects.unshift({
    id: 'underlay', type: 'shape', name: 'Underlay', shape: 'rect',
    transform: { x: 0, y: 0, width: 20, height: 20, anchor: 'top-left', zIndex: -1 },
    style: { fill: '#ffffff' },
  });
  transparentOverlay.objects[1].transform.zIndex = 0;
  await assert.rejects(buildPackage(transparentOverlay), /transparent overlay images/,
    'transparent images above lower layers must be rejected before RGB565 flattening');
  const hiddenTransparentOverlay = structuredClone(transparentOverlay);
  hiddenTransparentOverlay.objects[1].visible = false;
  await buildPackage(hiddenTransparentOverlay);
  const negativeTransparentOverlay = structuredClone(transparentOverlay);
  negativeTransparentOverlay.objects[1].transform.zIndex = -1;
  negativeTransparentOverlay.objects[0].transform.zIndex = -2;
  await assert.rejects(buildPackage(negativeTransparentOverlay), /transparent overlay images/,
    'transparent overlay detection must use actual z-order, not assume non-negative z-indexes');
  fakeImageAlpha = 255;

  console.log('WFS Web package builder tests passed.');
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
