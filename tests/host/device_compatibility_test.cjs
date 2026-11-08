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

// Regression cases: schema-valid fields placed on the wrong firmware element.
function withObjectField(field, value, base = fixture) {
  const design = structuredClone(base);
  design.objects[0][field] = value;
  return design;
}
assert.ok(errorsFor(withObjectField('shape', 'circle')).some(error => error.includes('only valid on shape')),
  'shape metadata on a digital clock must be rejected');
assert.ok(errorsFor(withObjectField('weatherFields', ['temperature'])).some(error => error.includes('only valid on weather')),
  'weatherFields on a digital clock must be rejected');
assert.ok(errorsFor(withObjectField('showSeconds', true)).some(error => error.includes('only valid on digital clocks')) === false,
  'showSeconds remains valid on digital clocks');

const analogFace = structuredClone(fixture);
analogFace.objects[0].type = 'analog-clock';
delete analogFace.objects[0].dataBinding;
delete analogFace.objects[0].timeFormat;
delete analogFace.objects[0].showSeconds;
delete analogFace.objects[0].showAmPm;
delete analogFace.objects[0].clockMode;
assert.ok(errorsFor(withObjectField('showSeconds', true, analogFace)).some(error => error.includes('only valid on digital clocks')),
  'showSeconds on an analog clock must be rejected');
assert.ok(errorsFor(withObjectField('showAmPm', false, analogFace)).some(error => error.includes('only valid on digital clocks')),
  'showAmPm on an analog clock must be rejected');

assert.ok(errorsFor(withObjectField('clockMode', '', fixture)).some(error => error.includes('clock mode')),
  'an explicitly empty clockMode must be rejected');
assert.ok(errorsFor(withObjectField('showAmPm', true, fixture)).some(error => error.includes('AM/PM can only be enabled')),
  'AM/PM cannot be enabled in 24-hour mode');

const nonAsciiText = structuredClone(fixture);
nonAsciiText.objects.push({
  id: 'label', type: 'text', name: 'Unicode label',
  transform: { x: 0, y: 0, width: 80, height: 20 }, text: 'Xin chào'
});
assert.ok(errorsFor(nonAsciiText).some(error => error.includes('printable ASCII')),
  'non-ASCII text must be rejected before firmware parsing');

const nestedChildren = structuredClone(fixture);
nestedChildren.objects[0].children = [{
  id: 'child', type: 'text', name: 'Child',
  transform: { x: 0, y: 0, width: 10, height: 10 }, text: 'x'
}];
assert.ok(errorsFor(nestedChildren).some(error => error.includes('nested children')),
  'non-empty nested children must be rejected before firmware parsing');


const opacityAboveRange = structuredClone(fixture);
opacityAboveRange.objects[0].transform.opacity = 1.2;
assert.ok(errorsFor(opacityAboveRange).some(error => error.includes('opacity must be numeric')),
  'opacity above one must be rejected before the firmware parser rejects it');

const stringOpacity = structuredClone(fixture);
stringOpacity.objects[0].transform.opacity = '1';
assert.ok(errorsFor(stringOpacity).some(error => error.includes('opacity must be numeric')),
  'string opacity must not be coerced into a number');

const stringWeight = structuredClone(fixture);
stringWeight.objects[0].textStyle.fontWeight = '400';
assert.ok(errorsFor(stringWeight).some(error => error.includes('fontWeight must be numeric')),
  'string font weights must be rejected before firmware parsing');

const stringLetterSpacing = structuredClone(fixture);
stringLetterSpacing.objects[0].textStyle.letterSpacing = '0';
assert.ok(errorsFor(stringLetterSpacing).some(error => error.includes('letterSpacing must be numeric')),
  'string letter spacing must not be coerced into a number');

const nullGlyphOpacity = structuredClone(fixture);
nullGlyphOpacity.objects[0].textStyle.opacity = null;
assert.ok(errorsFor(nullGlyphOpacity).some(error => error.includes('advanced glyph styling')),
  'null glyph opacity must not be treated as the default value');

const stringStrokeWidth = structuredClone(fixture);
stringStrokeWidth.objects.push({
  id: 'outline', type: 'shape', name: 'Outline',
  transform: { x: 10, y: 10, width: 40, height: 40 },
  style: { fill: '#ffffff', strokeWidth: '1' }
});
assert.ok(errorsFor(stringStrokeWidth).some(error => error.includes('strokeWidth must be numeric')),
  'string shape stroke widths must be rejected before firmware parsing');

console.log('WFS device compatibility preflight tests passed.');
