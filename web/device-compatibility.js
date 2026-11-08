(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  if (root) root.WfsDeviceCompatibility = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const TYPE_CAPABILITY = Object.freeze({
    text: ['text', 'text.basic'],
    'digital-clock': ['digital-clock', 'clock.digital'],
    'analog-clock': ['analog-clock', 'clock.analog'],
    date: ['date', 'date.basic'],
    weather: ['weather', 'weather.temperature'],
    shape: ['shape', 'shape.basic'],
    image: ['image', 'image.rgb565']
  });
  const SUPPORTED_TYPES = new Set(Object.keys(TYPE_CAPABILITY));
  const SAFE_FONTS = new Set(['system-ui', 'sans-serif', 'inherit']);
  const SUPPORTED_FORMAT_TOKENS = Object.freeze({
    'digital-clock': ['HH', 'mm', 'ss'],
    date: ['YYYY', 'ddd', 'MMM', 'MM', 'DD']
  });

  function hasOwn(object, key) {
    return Object.prototype.hasOwnProperty.call(object || {}, key);
  }

  function finiteNumber(value) {
    return typeof value === 'number' && Number.isFinite(value);
  }

  function unsupportedFormat(format, type) {
    if (typeof format !== 'string') return false;
    const tokens = SUPPORTED_FORMAT_TOKENS[type];
    if (!tokens) return false;
    for (let index = 0; index < format.length;) {
      const token = tokens.find(candidate => format.startsWith(candidate, index));
      if (token) {
        index += token.length;
        continue;
      }
      const character = format[index++];
      if (/[A-Za-z]/.test(character) || character.charCodeAt(0) < 0x20 ||
          character.charCodeAt(0) > 0x7e) return true;
    }
    return false;
  }

  function errors(design, capabilities, availableAssetIds) {
    const result = [];
    const caps = capabilities || {};
    const assetIds = availableAssetIds instanceof Set
      ? availableAssetIds : new Set(availableAssetIds || []);
    const advertisedElements = typeof caps.el === 'string'
      ? new Set(caps.el.split(',').map(value => value.trim()).filter(Boolean))
      : null;

    if (!design || typeof design !== 'object' || Array.isArray(design)) {
      return ['Design must be a JSON object'];
    }
    if (design.schemaVersion !== 1) result.push('Unsupported Watch Face schema version');
    if (design.deviceProfileId !== 'esp32-c3-gc9a01-240') {
      result.push('Design targets a different device profile');
    }
    if (!caps.upload) result.push('This firmware does not currently accept face packages');

    const canvas = design.canvas || {};
    if (canvas.width !== 240 || canvas.height !== 240) {
      result.push('This firmware requires a 240 × 240 logical canvas');
    }
    if (canvas.shape !== 'circle') {
      result.push('This device profile only supports a circular canvas');
    }

    const objects = Array.isArray(design.objects) ? design.objects : [];
    const maxObjects = Number.isInteger(caps.obj) && caps.obj >= 0 ? caps.obj : 16;
    if (objects.length > maxObjects) {
      result.push('This firmware supports at most ' + maxObjects + ' objects per face');
    }

    let jsonBytes = 0;
    try {
      jsonBytes = new TextEncoder().encode(JSON.stringify(design)).length;
    } catch (_) {
      result.push('Design cannot be serialized as UTF-8 JSON');
    }
    const maxJsonBytes = Number.isInteger(caps.json) && caps.json > 0 ? caps.json : 16384;
    if (jsonBytes > maxJsonBytes) {
      result.push('Design JSON exceeds the device limit of ' + maxJsonBytes + ' bytes');
    }

    for (const object of objects) {
      const name = typeof object?.name === 'string' && object.name
        ? object.name : (object?.id || 'Unnamed object');
      const type = object?.type;
      const transform = object?.transform || {};
      const textStyle = object?.textStyle || {};
      const binding = object?.dataBinding || {};
      const style = object?.style || {};
      const handConfig = object?.handConfig || {};
      const renderHints = object?.renderHints || {};

      if (!SUPPORTED_TYPES.has(type)) {
        result.push(name + ': element ' + String(type) + ' is not supported by this firmware');
        continue;
      }

      // Mirror the firmware parser's element-specific property contract. The
      // JSON schema permits these fields on more than one element type.
      if (hasOwn(object, 'shape') && type !== 'shape') {
        result.push(name + ': shape is only valid on shape elements');
      }
      if (hasOwn(object, 'weatherFields') && type !== 'weather') {
        result.push(name + ': weatherFields is only valid on weather elements');
      }
      if (hasOwn(object, 'showSeconds') && type !== 'digital-clock') {
        result.push(name + ': showSeconds is only valid on digital clocks');
      }
      if (hasOwn(object, 'showAmPm') && type !== 'digital-clock') {
        result.push(name + ': showAmPm is only valid on digital clocks');
      }
      if (hasOwn(object, 'children') &&
          (!Array.isArray(object.children) || object.children.length > 0)) {
        result.push(name + ': nested children are not supported by this firmware');
      }
      if (typeof object.text === 'string' &&
          [...object.text].some(character => {
            const code = character.charCodeAt(0);
            return code < 0x20 || code > 0x7e;
          })) {
        result.push(name + ': text must contain printable ASCII characters only');
      }

      const [elementName, capabilityName] = TYPE_CAPABILITY[type];
      if (advertisedElements && !advertisedElements.has(elementName)) {
        result.push(name + ': device does not advertise ' + elementName);
      }
      if (hasOwn(caps, 'img') && type === 'image' && !caps.img) {
        result.push(name + ': RGB565 image assets are not enabled by this firmware');
      }
      if (hasOwn(caps, 'el') && advertisedElements && !advertisedElements.has(elementName)) {
        // The element-level capability is authoritative for this firmware profile.
      }

      for (const key of ['x', 'y', 'width', 'height']) {
        if (!finiteNumber(transform[key])) {
          result.push(name + ': transform.' + key + ' must be a finite number');
        }
      }
      if (finiteNumber(transform.x) && (transform.x < -1024 || transform.x > 2048)) {
        result.push(name + ': x is outside the firmware parser range');
      }
      if (finiteNumber(transform.y) && (transform.y < -1024 || transform.y > 2048)) {
        result.push(name + ': y is outside the firmware parser range');
      }
      if (finiteNumber(transform.width) && (transform.width <= 0 || transform.width > 2048)) {
        result.push(name + ': width is outside the firmware parser range');
      }
      if (finiteNumber(transform.height) && (transform.height <= 0 || transform.height > 2048)) {
        result.push(name + ': height is outside the firmware parser range');
      }
      if (transform.rotation !== undefined && transform.rotation !== 0) {
        result.push(name + ': rotation is not supported yet');
      }
      if (transform.opacity !== undefined &&
          (!finiteNumber(transform.opacity) || transform.opacity < 0.999 ||
           transform.opacity > 1)) {
        result.push(name + ': opacity must be numeric and fully opaque on this firmware');
      }
      if (transform.anchor !== undefined && transform.anchor !== 'top-left') {
        result.push(name + ': only top-left object anchoring is supported');
      }

      if (type === 'image') {
        if (!caps.img) result.push(name + ': RGB565 image assets are not enabled by this firmware');
        if (!assetIds.has(object.assetId)) result.push(name + ': source image asset is missing from this browser');
        if (!finiteNumber(transform.width) || !finiteNumber(transform.height) ||
            transform.width < 1 || transform.height < 1 ||
            transform.width > 240 || transform.height > 240) {
          result.push(name + ': image dimensions must be between 1 and 240 pixels');
        }
        const width = Math.round(transform.width);
        const height = Math.round(transform.height);
        if (width < 1 || height < 1 || width * height * 2 > 115200) {
          result.push(name + ': converted RGB565 image exceeds the 240 × 240 asset limit');
        }
      }

      const font = textStyle.fontFamily;
      if (font && !SAFE_FONTS.has(font)) {
        result.push(name + ': font ' + font + ' is not available on the device yet');
      }
      if (textStyle.fontSize !== undefined &&
          (!finiteNumber(textStyle.fontSize) || textStyle.fontSize <= 0 || textStyle.fontSize > 256)) {
        result.push(name + ': font size is outside the device range');
      }
      if (textStyle.fontWeight !== undefined &&
          (!finiteNumber(textStyle.fontWeight) || ![400, 600].includes(textStyle.fontWeight))) {
        result.push(name + ': fontWeight must be numeric; this firmware supports 400 and 600 only');
      }
      if (textStyle.letterSpacing !== undefined &&
          (!finiteNumber(textStyle.letterSpacing) || textStyle.letterSpacing !== 0)) {
        result.push(name + ': letterSpacing must be numeric zero; nonzero spacing is not supported yet');
      }
      if (textStyle.scaleX !== undefined &&
          (!finiteNumber(textStyle.scaleX) || textStyle.scaleX < 0.1 || textStyle.scaleX > 8)) {
        result.push(name + ': horizontal glyph scale must be between 0.1 and 8');
      }
      if (textStyle.scaleY !== undefined &&
          (!finiteNumber(textStyle.scaleY) || textStyle.scaleY < 0.1 || textStyle.scaleY > 8)) {
        result.push(name + ': vertical glyph scale must be between 0.1 and 8');
      }
      if (['fontId', 'width', 'height'].some(key => hasOwn(textStyle, key))) {
        result.push(name + ': custom glyph font metrics are not supported yet');
      }
      const glyphNumericDefaults = [
        ['strokeWidth', 0], ['backgroundRadius', 0], ['opacity', 1],
        ['rotation', 0], ['zIndex', 0]
      ];
      const invalidGlyphNumeric = glyphNumericDefaults.some(([key, expected]) =>
        hasOwn(textStyle, key) &&
        (!finiteNumber(textStyle[key]) || textStyle[key] !== expected));
      if ((textStyle.stroke && textStyle.stroke !== 'none') ||
          invalidGlyphNumeric || textStyle.backgroundColor ||
          (textStyle.backgroundShape && textStyle.backgroundShape !== 'none') ||
          textStyle.visible === false) {
        result.push(name + ': advanced glyph styling is not supported yet');
      }
      if (style.backgroundColor ||
          (style.strokeWidth !== undefined &&
           (!finiteNumber(style.strokeWidth) ||
            (style.strokeWidth !== 0 && style.strokeWidth !== 1)))) {
        result.push(name + ': strokeWidth must be numeric zero or one; separate background fill is not supported');
      }

      if (type === 'digital-clock' && binding.source !== 'time') {
        result.push(name + ': digital clock requires the time binding');
      }
      if (type === 'date' && binding.source !== 'date') {
        result.push(name + ': date requires the date binding');
      }
      if (type === 'weather' && binding.source !== 'temperature') {
        result.push(name + ': only temperature weather binding is supported currently');
      }
      if (binding.timezone || binding.fallbackText || binding.visibleWhenMissing === true) {
        result.push(name + ': this data-binding option is not supported by the device');
      }
      if (binding.source && !(
        (type === 'digital-clock' && binding.source === 'time') ||
        (type === 'date' && binding.source === 'date') ||
        (type === 'weather' && binding.source === 'temperature')
      )) {
        result.push(name + ': this data binding is not supported by the device');
      }
      if (binding.format && unsupportedFormat(binding.format, type)) {
        result.push(name + ': data format contains unsupported tokens');
      }
      if (hasOwn(object, 'weatherFields') && !Array.isArray(object.weatherFields)) {
        result.push(name + ': weatherFields must be an array');
      }
      if (Array.isArray(object.weatherFields) &&
          object.weatherFields.some(field => !['temperature', 'condition'].includes(field))) {
        result.push(name + ': only temperature and condition weather fields are supported');
      }
      if (type === 'shape' && !['rect', 'circle', 'rounded-rect', 'line'].includes(object.shape || 'rect')) {
        result.push(name + ': unsupported shape kind');
      }
      if (Array.isArray(object.glyphOverrides) && object.glyphOverrides.length) {
        result.push(name + ': per-glyph overrides are not supported yet');
      }
      if (object.glyphAtlasAssetId) {
        result.push(name + ': custom glyph atlases are not supported by this firmware build');
      }
      if (object.glyphFallback && object.glyphFallback !== 'compatibility-error') {
        result.push(name + ': selected glyph fallback mode is not supported');
      }
      if (Object.values(handConfig).some(value =>
          value && typeof value === 'object' && Object.keys(value).length > 0)) {
        result.push(name + ': custom analog hand configuration is not supported yet');
      }
      if (renderHints.requiresAntialias === true || renderHints.requiresBitmap === true) {
        result.push(name + ': requested render hint is not supported by the device');
      }
      if (hasOwn(object, 'clockMode') &&
          ((type === 'analog-clock' && object.clockMode !== 'analog') ||
           (type === 'digital-clock' && object.clockMode !== 'digital') ||
           !['analog-clock', 'digital-clock'].includes(type))) {
        result.push(name + ': clock mode does not match the selected clock element');
      }
      if (hasOwn(object, 'showSeconds') && typeof object.showSeconds !== 'boolean') {
        result.push(name + ': showSeconds must be a boolean');
      }
      if (hasOwn(object, 'showAmPm') && typeof object.showAmPm !== 'boolean') {
        result.push(name + ': showAmPm must be a boolean');
      }
      if (object.showAmPm === true && object.timeFormat !== '12h') {
        result.push(name + ': AM/PM can only be enabled in 12-hour mode');
      }
      if (object.showAmPm === false && object.timeFormat === '12h') {
        result.push(name + ': hiding AM/PM in 12-hour mode is not supported yet');
      }
    }

    return [...new Set(result)];
  }

  return Object.freeze({ errors });
});
