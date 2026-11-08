# Phase 1 — Web/Firmware Compatibility Alignment

**Target branch:** \`rebuild/watch-face-studio-v2\`  
**Contract:** Watch Face schema v1 + WFS1 package v1  
**Device profile:** \`esp32-c3-gc9a01-240\`

## Scope

This change aligns the Web preflight and committed golden fixture with the firmware's currently implemented parser and renderer. It does not claim that every schema-valid design can be rendered by the ESP32-C3.

## Findings corrected

1. The old \`minimal-watch-face-v1.json\` fixture included a \`background\` object. The schema permits it, but \`FaceElementType\` and \`parseType()\` do not. The canvas background color already provides the same base fill, so the fixture now uses only a digital-clock element.
2. The old fixture supplied a timezone override, but the current firmware parser deliberately rejects non-empty binding timezone values. The fixture now uses the device's configured local time.
3. The old fixture selected font weight 700, while \`validateFaceModelForRenderer()\` accepts only 400 and 600. The fixture now selects 600.
4. The device profile previously had empty nominal limits and advertised a separate legacy capability vocabulary. It now declares the implemented WFS element capabilities and explicit static object/bitmap limits. Dynamic staging capacity remains a runtime capability and is intentionally not duplicated as a fixed profile limit.
5. Web preflight previously checked a hard-coded set of types and a subset of properties. It now uses a pure, host-testable compatibility module that checks the device's advertised element set, 240×240 circular canvas, object/JSON limits, transform/parser ranges, image availability/size, typography restrictions, binding formats, weather fields, and unsupported glyph/hand/render options before package creation.

## Runtime capability contract

The WFS capability response remains authoritative for the connected device. The static device profile is descriptive metadata and a conservative baseline; it must not override runtime values such as free storage, staging capacity, negotiated MTU, or upload availability.

| Capability | Firmware behavior |
|---|---|
| \`text.basic\` | ASCII text, bitmap glyph renderer |
| \`clock.digital\` | HH/mm/ss tokens; 12h/24h mode |
| \`clock.analog\` | Built-in analog face style |
| \`date.basic\` | YYYY, ddd, MMM, MM, DD tokens |
| \`weather.temperature\` | Temperature and condition text only |
| \`shape.basic\` | Rect, rounded rect, circle, line; no arc |
| \`image.rgb565\` | RGB565 little-endian assets, at most 240×240 |

## Verification

The Node host test loads the actual committed golden fixture and device profile, then exercises the same compatibility module called by the browser package builder. It verifies acceptance of the supported fixture and rejection of schema-valid but firmware-unsupported element types, weather fields, font weights, missing images, oversized object counts, non-device canvas dimensions, and devices without upload capability.

This test is a compatibility preflight test. It does not replace the C++ JSON parser, package validator, SPIFFS integration tests, PlatformIO build, or physical Web-BLE-device integration test.
