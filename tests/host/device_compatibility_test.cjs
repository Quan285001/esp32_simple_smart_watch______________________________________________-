const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const compatibility = require('../../web/device-compatibility.js');

const root = path.resolve(__dirname, '../..');
const fixture = JSON.parse(fs.readFileSync(
  path.join(root, 'shared/schemas/examples/minimal-watch-face-v1.json'), 'utf8'));
const profile = JSON.parse(fs.readFileSync(
  path.join(root, 'shared/capabilities/esp32-c3-gc9a01-240.json'), 'utf8'));

const capabilities = {
  upload: 1,
  obj: profile.nominalLimits.maxObjects,
  json: 16384,
  stage: 128 * 1024,
  img: 1,
  el: 'text,digital-clock,analog-clock,date,weather,shape,image'
};

function errorsFor(design, assetIds = []) {
  return compatibility.errors(design, capabilities, new Set(assetIds));
}

assert.deepEqual(errorsFor(fixture), [],
  'committed minimal fixture must be accepted by the device compatibility preflight');

const badCanvas = structuredClone(fixture);
badCanvas.canvas.width = 320;
assert.ok(errorsFor(badCanvas).some(error => error.includes('240 × 240')),
  'non-device canvas dimensions must be rejected');

const unsupportedType = structuredClone(fixture);
unsupportedType.objects.push({
  id: 'unsupported-background', type: 'background', name: 'Background',
  transform: { x: 0, y: 0, width: 240, height: 240 }
});
assert.ok(errorsFor(unsupportedType).some(error => error.includes('not supported')),
  'schema-valid but firmware-unsupported element types must be rejected');

const unsupportedWeather = structuredClone(fixture);
unsupportedWeather.objects.push({
  id: 'wind', type: 'weather', name: 'Wind',
  transform: { x: 0, y: 0, width: 80, height: 20 },
  dataBinding: { source: 'temperature' },
  weatherFields: ['wind-speed']
});
assert.ok(errorsFor(unsupportedWeather).some(error => error.includes('weather fields')),
  'unsupported weather fields must be rejected before packaging');

const unsupportedWeight = structuredClone(fixture);
unsupportedWeight.objects[0].textStyle.fontWeight = 700;
assert.ok(errorsFor(unsupportedWeight).some(error => error.includes('fontWeight') || error.includes('weights only')),
  'unsupported renderer font weights must be rejected');

const missingImage = structuredClone(fixture);
missingImage.objects.push({
  id: 'logo', type: 'image', name: 'Logo', assetId: 'not-in-browser',
  transform: { x: 10, y: 10, width: 32, height: 32 }
});
assert.ok(errorsFor(missingImage).some(error => error.includes('source image asset')),
  'image references without a browser asset must be rejected');

const tooMany = structuredClone(fixture);
tooMany.objects = Array.from({ length: 17 }, (_, index) => ({
  id: 'text-' + index, type: 'text', name: 'Text ' + index,
  transform: { x: 0, y: 0, width: 10, height: 10 }, text: 'x'
}));
assert.ok(errorsFor(tooMany).some(error => error.includes('at most')),
  'object-count limit must match the firmware capability');

const uploadDisabled = compatibility.errors(fixture, { ...capabilities, upload: 0 }, []);
assert.ok(uploadDisabled.some(error => error.includes('does not currently accept')),
  'a device that does not advertise upload must be rejected');

console.log('WFS device compatibility preflight tests passed.');
