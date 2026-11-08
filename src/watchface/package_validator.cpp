#include "package_validator.h"

#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "face_parser.h"
#include "face_renderer.h"
#include "../display/display_profile.h"

namespace {
constexpr size_t WFS_HEADER_SIZE = 64;
constexpr size_t MAX_MANIFEST_BYTES = 8192;
constexpr size_t MAX_LAYOUT_BYTES = 16384;
constexpr uint8_t WFS_MAGIC[4] = {'W', 'F', 'S', '1'};

uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint32_t crc32Iso(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

bool hexToBytes(const char* hex, uint8_t* out, size_t count) {
    if (hex == nullptr || out == nullptr || strlen(hex) != count * 2) return false;
    for (size_t i = 0; i < count; ++i) {
        char pair[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        char* end = nullptr;
        const unsigned long value = strtoul(pair, &end, 16);
        if (end == nullptr || *end != '\0' || value > 255) return false;
        out[i] = static_cast<uint8_t>(value);
    }
    return true;
}

bool digestFileRange(fs::File& file, size_t offset, size_t length, uint8_t output[32]) {
    if (!file.seek(offset, SeekSet)) return false;
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    if (mbedtls_sha256_starts_ret(&context, 0) != 0) {
        mbedtls_sha256_free(&context);
        return false;
    }
    uint8_t buffer[512];
    size_t remaining = length;
    while (remaining > 0) {
        const size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const size_t read = file.read(buffer, wanted);
        if (read != wanted || mbedtls_sha256_update_ret(&context, buffer, read) != 0) {
            mbedtls_sha256_free(&context);
            return false;
        }
        remaining -= read;
    }
    const int result = mbedtls_sha256_finish_ret(&context, output);
    mbedtls_sha256_free(&context);
    return result == 0;
}

bool hasOnlyKeys(JsonObjectConst object, const char* const* allowedKeys, size_t allowedCount) {
    for (JsonPairConst pair : object) {
        const char* key = pair.key().c_str();
        bool found = false;
        for (size_t i = 0; i < allowedCount; ++i) {
            if (strcmp(key, allowedKeys[i]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

uint16_t capabilityBit(const char* value) {
    if (value == nullptr) return 0;
    if (strcmp(value, "text.basic") == 0) return 1U << 0;
    if (strcmp(value, "clock.digital") == 0) return 1U << 1;
    if (strcmp(value, "clock.analog") == 0) return 1U << 2;
    if (strcmp(value, "date.basic") == 0) return 1U << 3;
    if (strcmp(value, "weather.temperature") == 0) return 1U << 4;
    if (strcmp(value, "weather.condition") == 0) return 1U << 5;
    if (strcmp(value, "shape.basic") == 0) return 1U << 6;
    if (strcmp(value, "image.rgb565") == 0) return 1U << 7;
    return 0;
}

bool isSupportedCapability(const char* value) {
    return capabilityBit(value) != 0;
}


}

bool validateWfsPackage(fs::File& file, size_t expectedFileLength,
                        const uint8_t* expectedPayloadDigest,
                        const char* expectedDesignId,
                        FaceModel& output,
                        PackageValidationError& error) {
    error = PackageValidationError::None;
    memset(&output, 0, sizeof(output));

    if (!file || expectedFileLength < WFS_HEADER_SIZE || file.size() != expectedFileLength) {
        error = PackageValidationError::Io;
        return false;
    }

    uint8_t header[WFS_HEADER_SIZE] = {};
    if (!file.seek(0, SeekSet) || file.read(header, sizeof(header)) != sizeof(header)) {
        error = PackageValidationError::Io;
        return false;
    }
    if (memcmp(header, WFS_MAGIC, sizeof(WFS_MAGIC)) != 0 ||
        readU16(header + 6) != WFS_HEADER_SIZE ||
        readU16(header + 18) != 0 || readU32(header + 60) != 0) {
        error = PackageValidationError::Header;
        return false;
    }
    if (readU16(header + 4) != 1) {
        error = PackageValidationError::Version;
        return false;
    }

    const uint32_t manifestLength = readU32(header + 8);
    const uint32_t layoutLength = readU32(header + 12);
    const uint16_t assetCount = readU16(header + 16);
    const uint32_t payloadLength = readU32(header + 20);
    if (manifestLength == 0 || manifestLength > MAX_MANIFEST_BYTES ||
        layoutLength == 0 || layoutLength > MAX_LAYOUT_BYTES ||
        assetCount > DisplayProfileConfig::MAX_ASSETS || assetCount > FACE_MAX_ASSETS ||
        payloadLength != expectedFileLength - WFS_HEADER_SIZE ||
        static_cast<uint64_t>(manifestLength) + layoutLength > payloadLength) {
        error = PackageValidationError::Length;
        return false;
    }
    if (crc32Iso(header, 56) != readU32(header + 56)) {
        error = PackageValidationError::HeaderCrc;
        return false;
    }

    uint8_t headerDigest[32] = {};
    memcpy(headerDigest, header + 24, sizeof(headerDigest));
    if (expectedPayloadDigest != nullptr &&
        memcmp(headerDigest, expectedPayloadDigest, sizeof(headerDigest)) != 0) {
        error = PackageValidationError::PayloadDigest;
        return false;
    }

    uint8_t actualPayloadDigest[32] = {};
    if (!digestFileRange(file, WFS_HEADER_SIZE, payloadLength, actualPayloadDigest) ||
        memcmp(actualPayloadDigest, headerDigest, sizeof(headerDigest)) != 0) {
        error = PackageValidationError::PayloadDigest;
        return false;
    }

    char* manifestText = static_cast<char*>(malloc(manifestLength + 1));
    char* layoutText = nullptr;
    if (manifestText == nullptr) {
        error = PackageValidationError::Io;
        return false;
    }

    const bool readOk = file.seek(WFS_HEADER_SIZE, SeekSet) &&
                        file.read(reinterpret_cast<uint8_t*>(manifestText), manifestLength) == manifestLength;
    if (!readOk) {
        free(manifestText);
        error = PackageValidationError::Io;
        return false;
    }
    manifestText[manifestLength] = '\0';

    JsonDocument manifest;
    const DeserializationError manifestError =
        deserializeJson(manifest, manifestText, manifestLength,
                        DeserializationOption::NestingLimit(8));
    static const char* const manifestKeys[] = {
        "packageFormatVersion", "schemaVersion", "designId", "name",
        "deviceProfileId", "layoutSha256", "requiredCapabilities", "assets"
    };
    JsonObjectConst manifestObject = manifest.as<JsonObjectConst>();
    const char* profileId = manifest["deviceProfileId"] | "";
    if (manifestError || manifestObject.isNull() ||
        !hasOnlyKeys(manifestObject, manifestKeys,
                     sizeof(manifestKeys) / sizeof(manifestKeys[0])) ||
        (manifest["packageFormatVersion"] | 0) != 1 ||
        (manifest["schemaVersion"] | 0) != 1 ||
        strcmp(profileId, DisplayProfileConfig::ID) != 0) {
        free(manifestText);
        free(layoutText);
        error = PackageValidationError::Manifest;
        return false;
    }

    const char* designId = manifest["designId"] | "";
    char manifestDesignId[FACE_ID_CAPACITY] = {};
    const size_t manifestDesignIdLength = strlen(designId);
    if (manifestDesignIdLength == 0 ||
        manifestDesignIdLength >= sizeof(manifestDesignId) ||
        (expectedDesignId != nullptr && strcmp(designId, expectedDesignId) != 0)) {
        free(manifestText);
        free(layoutText);
        error = PackageValidationError::DesignIdMismatch;
        return false;
    }
    memcpy(manifestDesignId, designId, manifestDesignIdLength + 1);

    uint8_t expectedLayoutDigest[32] = {};
    uint8_t actualLayoutDigest[32] = {};
    if (!hexToBytes(manifest["layoutSha256"] | "", expectedLayoutDigest, 32) ||
        !digestFileRange(file, WFS_HEADER_SIZE + manifestLength, layoutLength, actualLayoutDigest) ||
        memcmp(expectedLayoutDigest, actualLayoutDigest, 32) != 0) {
        free(manifestText);
        free(layoutText);
        error = PackageValidationError::LayoutDigest;
        return false;
    }

    JsonArrayConst assets = manifest["assets"].as<JsonArrayConst>();
    if (assets.isNull() || assets.size() != assetCount) {
        free(manifestText);
        free(layoutText);
        error = PackageValidationError::AssetManifest;
        return false;
    }

    uint32_t expectedAssetOffset = 0;
    FaceAsset parsedAssets[FACE_MAX_ASSETS] = {};
    uint8_t parsedAssetCount = 0;
    static const char* const assetKeys[] = {
        "assetId", "mediaType", "encoding", "width", "height",
        "byteLength", "offset", "sha256"
    };
    for (JsonObjectConst asset : assets) {
        if (asset.isNull() ||
            !hasOnlyKeys(asset, assetKeys, sizeof(assetKeys) / sizeof(assetKeys[0]))) {
            free(manifestText);
            free(layoutText);
            error = PackageValidationError::AssetManifest;
            return false;
        }
        const char* assetId = asset["assetId"] | "";
        const char* encoding = asset["encoding"] | "";
        const char* mediaType = asset["mediaType"] | "";
        const char* assetShaText = asset["sha256"] | "";
        const uint32_t width = asset["width"] | 0;
        const uint32_t height = asset["height"] | 0;
        const uint32_t byteLength = asset["byteLength"] | 0;
        const uint32_t offset = asset["offset"] | 0;
        uint8_t expectedAssetDigest[32] = {};
        uint8_t actualAssetDigest[32] = {};

        if (assetId[0] == '\0' || strlen(assetId) >= FACE_ASSET_ID_CAPACITY ||
            parsedAssetCount >= DisplayProfileConfig::MAX_ASSETS || parsedAssetCount >= FACE_MAX_ASSETS || strcmp(encoding, "rgb565le") != 0 ||
            strcmp(mediaType, "image/rgb565") != 0 ||
            width == 0 || width > DisplayProfileConfig::MAX_BITMAP_WIDTH ||
            height == 0 || height > DisplayProfileConfig::MAX_BITMAP_HEIGHT ||
            byteLength != width * height * 2U || offset != expectedAssetOffset ||
            !hexToBytes(assetShaText, expectedAssetDigest, 32)) {
            free(manifestText);
            free(layoutText);
            error = PackageValidationError::AssetManifest;
            return false;
        }

        for (uint8_t previous = 0; previous < parsedAssetCount; ++previous) {
            if (strcmp(parsedAssets[previous].id, assetId) == 0) {
                free(manifestText);
                free(layoutText);
                error = PackageValidationError::AssetManifest;
                return false;
            }
        }
        FaceAsset& parsedAsset = parsedAssets[parsedAssetCount];
        strncpy(parsedAsset.id, assetId, sizeof(parsedAsset.id) - 1);
        parsedAsset.width = static_cast<uint16_t>(width);
        parsedAsset.height = static_cast<uint16_t>(height);
        parsedAsset.packageOffset = static_cast<uint32_t>(WFS_HEADER_SIZE + manifestLength + layoutLength + offset);
        parsedAsset.byteLength = byteLength;
        ++parsedAssetCount;

        const size_t absoluteOffset = WFS_HEADER_SIZE + manifestLength + layoutLength + offset;
        if (!digestFileRange(file, absoluteOffset, byteLength, actualAssetDigest) ||
            memcmp(expectedAssetDigest, actualAssetDigest, 32) != 0) {
            free(manifestText);
            free(layoutText);
            error = PackageValidationError::AssetDigest;
            return false;
        }
        expectedAssetOffset += byteLength;
    }

    if (static_cast<uint64_t>(manifestLength) + layoutLength + expectedAssetOffset != payloadLength) {
        free(manifestText);
        free(layoutText);
        error = PackageValidationError::Length;
        return false;
    }

    uint16_t declaredCapabilityMask = 0;
    JsonArrayConst requiredCapabilities = manifest["requiredCapabilities"].as<JsonArrayConst>();
    if (requiredCapabilities.isNull()) {
        free(manifestText);
        free(layoutText);
        error = PackageValidationError::Manifest;
        return false;
    }
    for (JsonVariantConst capability : requiredCapabilities) {
        if (!capability.is<const char*>() || !isSupportedCapability(capability.as<const char*>())) {
            free(manifestText);
            free(layoutText);
            error = PackageValidationError::UnsupportedCapability;
            return false;
        }
        declaredCapabilityMask |= capabilityBit(capability.as<const char*>());
    }

    // Release manifest parsing memory before allocating the layout parser
    // document; both JSON trees are never kept alive at the same time.
    free(manifestText);
    manifestText = nullptr;
    manifest.clear();

    // Allocate and read the layout only after the manifest JSON tree and text
    // buffer are released, reducing peak heap use during package validation.
    layoutText = static_cast<char*>(malloc(layoutLength + 1));
    if (layoutText == nullptr) {
        error = PackageValidationError::Io;
        return false;
    }
    if (!file.seek(WFS_HEADER_SIZE + manifestLength, SeekSet) ||
        file.read(reinterpret_cast<uint8_t*>(layoutText), layoutLength) != layoutLength) {
        free(layoutText);
        error = PackageValidationError::Io;
        return false;
    }
    layoutText[layoutLength] = '\0';

    const FaceParseResult parseResult =
        parseWatchFaceJson(layoutText, layoutLength, output);
    free(layoutText);
    if (!parseResult) {
        error = PackageValidationError::LayoutSchema;
        return false;
    }
    if (strcmp(output.designId, manifestDesignId) != 0 ||
        strcmp(output.deviceProfileId, DisplayProfileConfig::ID) != 0) {
        error = PackageValidationError::DesignIdMismatch;
        return false;
    }

    output.assetCount = parsedAssetCount;
    for (uint8_t i = 0; i < parsedAssetCount; ++i) output.assets[i] = parsedAssets[i];

    bool referenced[FACE_MAX_ASSETS] = {};
    uint16_t requiredCapabilityMask = 0;
    for (uint8_t i = 0; i < output.objectCount; ++i) {
        const FaceObject& object = output.objects[i];
        switch (object.type) {
            case FaceElementType::Text:
                requiredCapabilityMask |= capabilityBit("text.basic");
                break;
            case FaceElementType::DigitalClock:
                requiredCapabilityMask |= capabilityBit("clock.digital");
                break;
            case FaceElementType::AnalogClock:
                requiredCapabilityMask |= capabilityBit("clock.analog");
                break;
            case FaceElementType::Date:
                requiredCapabilityMask |= capabilityBit("date.basic");
                break;
            case FaceElementType::Weather:
                requiredCapabilityMask |= capabilityBit("weather.temperature");
                if (object.showWeatherCondition) {
                    requiredCapabilityMask |= capabilityBit("weather.condition");
                }
                break;
            case FaceElementType::Shape:
                requiredCapabilityMask |= capabilityBit("shape.basic");
                break;
            case FaceElementType::Image:
                requiredCapabilityMask |= capabilityBit("image.rgb565");
                break;
        }
        if (object.type != FaceElementType::Image) continue;
        bool found = false;
        for (uint8_t a = 0; a < output.assetCount; ++a) {
            const FaceAsset& asset = output.assets[a];
            if (strcmp(asset.id, object.assetId) == 0) {
                const uint16_t objectWidth = static_cast<uint16_t>(lroundf(object.transform.width));
                const uint16_t objectHeight = static_cast<uint16_t>(lroundf(object.transform.height));
                if (asset.width != objectWidth || asset.height != objectHeight) {
                    error = PackageValidationError::AssetManifest;
                    return false;
                }
                referenced[a] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            error = PackageValidationError::AssetManifest;
            return false;
        }
    }
    if ((declaredCapabilityMask & requiredCapabilityMask) != requiredCapabilityMask) {
        error = PackageValidationError::UnsupportedCapability;
        return false;
    }
    for (uint8_t a = 0; a < output.assetCount; ++a) {
        if (!referenced[a]) {
            error = PackageValidationError::AssetManifest;
            return false;
        }
    }

    if (validateFaceModelForRenderer(output) != FaceRenderStatus::Ok) {
        error = PackageValidationError::UnsupportedCapability;
        return false;
    }
    return true;
}

const char* packageValidationErrorName(PackageValidationError error) {
    switch (error) {
        case PackageValidationError::None: return "NONE";
        case PackageValidationError::Io: return "IO";
        case PackageValidationError::Header: return "HEADER";
        case PackageValidationError::Version: return "VERSION";
        case PackageValidationError::Length: return "LENGTH";
        case PackageValidationError::HeaderCrc: return "HEADER_CRC";
        case PackageValidationError::PayloadDigest: return "PAYLOAD_DIGEST";
        case PackageValidationError::Manifest: return "MANIFEST";
        case PackageValidationError::LayoutDigest: return "LAYOUT_DIGEST";
        case PackageValidationError::LayoutSchema: return "LAYOUT_SCHEMA";
        case PackageValidationError::DesignIdMismatch: return "DESIGN_ID_MISMATCH";
        case PackageValidationError::AssetManifest: return "ASSET_MANIFEST";
        case PackageValidationError::AssetDigest: return "ASSET_DIGEST";
        case PackageValidationError::UnsupportedCapability: return "UNSUPPORTED_CAPABILITY";
        default: return "UNKNOWN";
    }
}
