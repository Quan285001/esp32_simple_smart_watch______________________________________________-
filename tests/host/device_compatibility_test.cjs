const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const compatibility = require('../../web/device-compatibility.js');

const root = path.resolve(__dirname, '../..');
const appSource = fs.readFileSync(path.join(root, 'web/app.js'), 'utf8');
const packageBuilder = appSource.slice(
  appSource.indexOf('async function buildWfsPackage'),
  appSource.indexOf('async function sendWfsChunkWithRetry')
);
assert.match(packageBuilder, /globalThis\.WfsDeviceCompatibility/,
  'the browser package builder must invoke the shared firmware compatibility preflight');
assert.match(packageBuilder, /typeof compatibilityModule\.errors!==\x27function\x27/,
  'package creation must fail closed when the shared compatibility module is unavailable');

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
  el: 't,dc,ac,d,w,s,i',
  wc: 1
};

function errorsFor(design, assetIds = []) {
  return compatibility.errors(design, capabilities, new Set(assetIds));
}

assert.deepEqual(errorsFor(fixture), [],
  'committed minimal fixture must be accepted by the device compatibility preflight');

assert.match(appSource, /id:'mono',name:'Monochrome'[^}]*weight:600/,
  'the built-in Monochrome template must use a renderer-supported font weight');

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

const conditionWithoutCapability = structuredClone(fixture);
conditionWithoutCapability.objects[0].type = 'weather';
conditionWithoutCapability.objects[0].dataBinding = { source: 'temperature' };
conditionWithoutCapability.objects[0].weatherFields = ['condition'];
delete conditionWithoutCapability.objects[0].clockMode;
delete conditionWithoutCapability.objects[0].timeFormat;
delete conditionWithoutCapability.objects[0].showSeconds;
delete conditionWithoutCapability.objects[0].showAmPm;
assert.ok(compatibility.errors(conditionWithoutCapability, { ...capabilities, wc: 0 }, [])
  .some(error => error.includes('weather-condition rendering')),
  'condition weather fields must be rejected when the device does not advertise them');
assert.ok(errorsFor(conditionWithoutCapability).length === 0,
  'condition weather fields must be accepted when the device advertises support');

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

const legacyTextFill = structuredClone(fixture);
delete legacyTextFill.objects[0].textStyle.fill;
legacyTextFill.objects[0].style = { fill: '#ff0000' };
assert.ok(errorsFor(legacyTextFill).some(error => error.includes('use textStyle.fill')),
  'text preview color fallbacks must not silently diverge from firmware text color');
const unsupportedTextStroke = withObjectField('style', { stroke: '#ff0000', strokeWidth: 1 });
assert.ok(errorsFor(unsupportedTextStroke).some(error => error.includes('only rendered for shape elements')),
  'non-shape borders must be rejected because the firmware renderer ignores them');

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

const nearlyOpaque = structuredClone(fixture);
nearlyOpaque.objects[0].transform.opacity = 0.9995;
assert.ok(errorsFor(nearlyOpaque).some(error => error.includes('fully opaque')),
  'opacity below one must be rejected because the firmware renderer has no alpha blending');

const rendererSource = fs.readFileSync(path.join(root, 'src/watchface/face_renderer.cpp'), 'utf8');
assert.match(rendererSource, /object\.transform\.opacity < 1\.0f/,
  'firmware renderer preflight must enforce the same fully opaque contract');

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

const fractionalCornerRadius = structuredClone(fixture);
fractionalCornerRadius.objects[0].type = 'shape';
fractionalCornerRadius.objects[0].shape = 'rounded-rect';
fractionalCornerRadius.objects[0].style = { cornerRadius: 1.5 };
assert.ok(errorsFor(fractionalCornerRadius).some(error => error.includes('cornerRadius must be an integer')),
  'fractional corner radii must be rejected before firmware truncates them');

const oversizedCornerRadius = structuredClone(fractionalCornerRadius);
oversizedCornerRadius.objects[0].style.cornerRadius = 256;
assert.ok(errorsFor(oversizedCornerRadius).some(error => error.includes('cornerRadius must be an integer')),
  'corner radii outside the firmware uint8 range must be rejected');

const misplacedCornerRadius = structuredClone(fixture);
misplacedCornerRadius.objects[0].style = { cornerRadius: 8 };
assert.ok(errorsFor(misplacedCornerRadius).some(error => error.includes('rendered only for rounded-rectangle')),
  'corner radius on a non-shape element must be rejected');

const validRoundedCornerRadius = structuredClone(fixture);
validRoundedCornerRadius.objects = [{
  id: 'rounded', type: 'shape', name: 'Rounded',
  transform: { x: 0, y: 0, width: 40, height: 40 },
  shape: 'rounded-rect', style: { fill: '#ffffff', cornerRadius: 8 }
}];
assert.deepEqual(errorsFor(validRoundedCornerRadius), [],
  'integer corner radius on a rounded rectangle must remain supported');

const stringStrokeWidth = structuredClone(fixture);
stringStrokeWidth.objects.push({
  id: 'outline', type: 'shape', name: 'Outline',
  transform: { x: 10, y: 10, width: 40, height: 40 },
  style: { fill: '#ffffff', strokeWidth: '1' }
});
assert.ok(errorsFor(stringStrokeWidth).some(error => error.includes('strokeWidth must be numeric')),
  'string shape stroke widths must be rejected before firmware parsing');

console.log('WFS device compatibility preflight tests passed.');
