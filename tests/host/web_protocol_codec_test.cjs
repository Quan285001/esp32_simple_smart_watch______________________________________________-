const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { webcrypto } = require('node:crypto');
const { TextEncoder, TextDecoder } = require('node:util');

const root = path.resolve(__dirname, '../..');
const appSource = fs.readFileSync(path.join(root, 'web/app.js'), 'utf8');
const sandbox = {
  document: {
    querySelector: () => null,
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
};
vm.runInNewContext(
  appSource + '\n;globalThis.__wfsCodecTest = { state, encode: wfsEncodeFrame, decode: wfsDecodeFrame, waitFor: waitForWfsMessage, buildPackage: buildWfsPackage, crc32: wfsCrc32, hex: wfsHex };',
  sandbox,
  { filename: 'web/app.js' },
);

const { state, encode, decode, waitFor, buildPackage, crc32, hex } = sandbox.__wfsCodecTest;
state.wfs = { txSequence: 7, sessionId: 0x12345678, pending: new Map() };
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
  state.wfsCapabilities = { obj: 16, json: 16384, stage: 32768, img: 1 };
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

  state.wfsCapabilities.stage = 64;
  await assert.rejects(buildPackage(design), /staging storage/,
    'package builder must reject a package larger than the reported staging capacity');

  console.log('WFS Web package builder tests passed.');
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
