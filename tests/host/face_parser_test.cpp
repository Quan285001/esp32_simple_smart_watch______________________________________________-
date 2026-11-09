#include "face_parser.h"

#include <assert.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string readFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

static std::string replaceOnce(std::string value, const std::string& from,
                               const std::string& to) {
    const size_t position = value.find(from);
    assert(position != std::string::npos);
    value.replace(position, from.size(), to);
    return value;
}

static FaceParseResult parse(const std::string& json, FaceModel& output) {
    return parseWatchFaceJson(json.c_str(), json.size(), output);
}

static const std::string TEXT_FACE = R"JSON({
  "schemaVersion": 1,
  "designId": "host-text",
  "name": "Host Text",
  "deviceProfileId": "esp32-c3-gc9a01-240",
  "canvas": {
    "width": 240,
    "height": 240,
    "shape": "circle",
    "backgroundColor": "#000000"
  },
  "objects": [{
    "id": "label",
    "type": "text",
    "name": "Label",
    "transform": {"x": 0, "y": 0, "width": 80, "height": 20},
    "text": "Hello"
  }]
})JSON";

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string fixture = readFile(argv[1]);
    FaceModel model{};

    FaceParseResult result = parse(fixture, model);
    assert(result && model.objectCount == 1);
    assert(model.objects[0].type == FaceElementType::DigitalClock);
    assert(model.canvasWidth == 240 && model.canvasHeight == 240);

    assert(!parse(replaceOnce(fixture, R"JSON("schemaVersion": 1)JSON",
                              R"JSON("schemaVersion": 1.5)JSON"), model));
    assert(!parse(replaceOnce(fixture, R"JSON("designId": "sample-minimal-digital")JSON",
                              R"JSON("designId": "")JSON"), model));
    assert(!parse(replaceOnce(fixture, R"JSON("name": "Minimal Digital")JSON",
                              R"JSON("name": "")JSON"), model));
    assert(!parse(replaceOnce(fixture, R"JSON("id": "time")JSON",
                              R"JSON("id": "")JSON"), model));
    assert(!parse(replaceOnce(fixture, R"JSON("name": "Digital Time")JSON",
                              R"JSON("name": "")JSON"), model));
    assert(!parse(replaceOnce(fixture, R"JSON("id": "time")JSON",
                              R"JSON("id": "invalid id")JSON"), model));
    assert(!parse(replaceOnce(fixture, R"JSON("source": "time")JSON",
                              R"JSON("source": "")JSON"), model));
    const std::string invalidImageAssetId = replaceOnce(
        replaceOnce(TEXT_FACE, R"JSON("type": "text")JSON", R"JSON("type": "image")JSON"),
        R"JSON("text": "Hello")JSON", R"JSON("assetId": "invalid id")JSON");
    assert(!parse(invalidImageAssetId, model));
    assert(parse(std::string(16385, ' '), model).error == FaceParseError::InputTooLarge);
    assert(!parse(replaceOnce(fixture, R"JSON("width": 240)JSON",
                              R"JSON("width": 240.5)JSON"), model));
    assert(!parse(replaceOnce(fixture, R"JSON("backgroundColor": "#080B10")JSON",
                              R"JSON("backgroundColor": null)JSON"), model));
    assert(!parse(replaceOnce(fixture, R"JSON("top": 12)JSON",
                              R"JSON("top": -1)JSON"), model));

    assert(!parse(replaceOnce(fixture,
                              R"JSON("deviceProfileId": "esp32-c3-gc9a01-240")JSON",
                              R"JSON("deviceProfileId": "unknown-display")JSON"), model));

    std::string invalidMetadata = replaceOnce(fixture, "\"objects\":",
        "\"metadata\": \"not-an-object\", \"objects\":");
    assert(!parse(invalidMetadata, model));

    std::string invalidDescription = replaceOnce(fixture, "\"objects\":",
        "\"description\": 123, \"objects\":");
    assert(!parse(invalidDescription, model));

    std::string invalidBinding = replaceOnce(TEXT_FACE, "\"text\": \"Hello\"",
        "\"text\": \"Hello\", \"dataBinding\": \"not-an-object\"");
    result = parse(invalidBinding, model);
    assert(!result && result.error == FaceParseError::UnsupportedProperty);

    std::string nonAscii = replaceOnce(TEXT_FACE, "Hello", "Xin chào");
    assert(!parse(nonAscii, model));

    std::string misplacedShape = replaceOnce(TEXT_FACE, "\"text\": \"Hello\"",
        "\"text\": \"Hello\", \"shape\": \"circle\"");
    assert(!parse(misplacedShape, model));

    std::string invalidClockMode = replaceOnce(fixture,
        "\"clockMode\": \"digital\"", "\"clockMode\": \"\"");
    assert(!parse(invalidClockMode, model));

    const std::string fractionalCornerRadius = replaceOnce(TEXT_FACE,
        "\"text\": \"Hello\"", "\"text\": \"Hello\", \"style\": {\"cornerRadius\": 1.5}");
    result = parse(fractionalCornerRadius, model);
    assert(!result && result.error == FaceParseError::UnsupportedProperty);

    const std::string oversizedCornerRadius = replaceOnce(TEXT_FACE,
        "\"text\": \"Hello\"", "\"text\": \"Hello\", \"style\": {\"cornerRadius\": 256}");
    result = parse(oversizedCornerRadius, model);
    assert(!result && result.error == FaceParseError::UnsupportedProperty);

    const std::string misplacedCornerRadius = replaceOnce(TEXT_FACE,
        "\"text\": \"Hello\"", "\"text\": \"Hello\", \"style\": {\"cornerRadius\": 8}");
    result = parse(misplacedCornerRadius, model);
    assert(!result && result.error == FaceParseError::UnsupportedProperty);

    std::string validRoundedShape = replaceOnce(TEXT_FACE, "\"type\": \"text\"", "\"type\": \"shape\"");
    validRoundedShape = replaceOnce(validRoundedShape, "\"text\": \"Hello\"",
        "\"shape\": \"rounded-rect\", \"style\": {\"cornerRadius\": 8}");
    result = parse(validRoundedShape, model);
    assert(result && model.objects[0].cornerRadius == 8);

    std::string fractionalStroke = replaceOnce(validRoundedShape,
        R"JSON("cornerRadius": 8)JSON",
        R"JSON("cornerRadius": 8, "strokeWidth": 0.5)JSON");
    result = parse(fractionalStroke, model);
    assert(!result && result.error == FaceParseError::InvalidField);

    std::string oversizedStroke = replaceOnce(validRoundedShape,
        R"JSON("cornerRadius": 8)JSON",
        R"JSON("cornerRadius": 8, "strokeWidth": 2)JSON");
    result = parse(oversizedStroke, model);
    assert(!result && result.error == FaceParseError::InvalidField);

    std::string supportedStroke = replaceOnce(validRoundedShape,
        R"JSON("cornerRadius": 8)JSON",
        R"JSON("cornerRadius": 8, "strokeWidth": 1)JSON");
    result = parse(supportedStroke, model);
    assert(result && model.objects[0].strokeWidth == 1);

    const std::string objectJson = R"JSON({
      "id": "label",
      "type": "text",
      "name": "Label",
      "transform": {"x": 0, "y": 0, "width": 80, "height": 20},
      "text": "Hello"
    })JSON";
    std::string manyObjects = R"JSON({
      "schemaVersion": 1,
      "designId": "many",
      "name": "Many",
      "deviceProfileId": "esp32-c3-gc9a01-240",
      "canvas": {"width": 240, "height": 240, "shape": "circle", "backgroundColor": "#000000"},
      "objects": [)JSON";
    for (int i = 0; i < 17; ++i) {
        if (i) manyObjects += ",";
        manyObjects += objectJson;
    }
    manyObjects += "]}";
    result = parse(manyObjects, model);
    assert(!result && result.error == FaceParseError::TooManyObjects);

    std::cout << "WFS Face parser host tests passed.\n";
    return 0;
}
