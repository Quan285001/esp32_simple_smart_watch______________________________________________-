const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const root = path.resolve(__dirname, '../..');
const appSource = fs.readFileSync(path.join(root, 'web/app.js'), 'utf8');
const sandbox = {
  document: {
    querySelector: () => null,
    querySelectorAll: () => [],
    addEventListener: () => {},
  },
};
vm.runInNewContext(
  appSource + '\n;globalThis.__wfsCodecTest = { state, encode: wfsEncodeFrame, decode: wfsDecodeFrame };',
  sandbox,
  { filename: 'web/app.js' },
);

const { state, encode, decode } = sandbox.__wfsCodecTest;
state.wfs = { txSequence: 7, sessionId: 0x12345678 };

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

console.log('WFS Web protocol codec tests passed.');
