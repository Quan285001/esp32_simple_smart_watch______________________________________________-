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

    result = parse(replaceOnce(fixture, "\"schemaVersion\": 1",
                               "\"schemaVersion\": 1.5"), model);
    assert(!result);

    result = parse(replaceOnce(fixture, "\"width\": 240",
                               "\"width\": 240.5"), model);
    assert(!result);

    result = parse(replaceOnce(fixture, "\"backgroundColor\": \"#080B10\"",
                               "\"backgroundColor\": null"), model);
    assert(!result);

    result = parse(replaceOnce(fixture, "\"description\":", "\"description\":"),
                   model);
    assert(result);

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

    std::string manyObjects = fixture;
    const std::string objectStart = "\"objects\": [";
    const size_t arrayStart = manyObjects.find(objectStart);
    assert(arrayStart != std::string::npos);
    const size_t firstObjectStart = manyObjects.find('{', arrayStart + objectStart.size());
    const size_t firstObjectEnd = manyObjects.find("\n    }", firstObjectStart);
    assert(firstObjectStart != std::string::npos && firstObjectEnd != std::string::npos);
    const std::string objectJson = manyObjects.substr(firstObjectStart, firstObjectEnd + 5 - firstObjectStart);
    manyObjects.replace(arrayStart + objectStart.size(),
                        manyObjects.find(']', arrayStart) - (arrayStart + objectStart.size()),
                        "");
    std::string objects;
    for (int i = 0; i < 17; ++i) {
        if (i) objects += ",";
        objects += objectJson;
    }
    const size_t insertAt = manyObjects.find('[', arrayStart) + 1;
    manyObjects.insert(insertAt, objects);
    result = parse(manyObjects, model);
    assert(!result && result.error == FaceParseError::TooManyObjects);

    std::cout << "WFS Face parser host tests passed.\n";
    return 0;
}
