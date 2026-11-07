#pragma once

#include <stddef.h>
#include <stdint.h>
#include <SPIFFS.h>

#include "face_model.h"

enum class PackageValidationError : uint8_t {
    None,
    Io,
    Header,
    Version,
    Length,
    HeaderCrc,
    PayloadDigest,
    Manifest,
    LayoutDigest,
    LayoutSchema,
    DesignIdMismatch,
    AssetManifest,
    AssetDigest,
    UnsupportedCapability
};

bool validateWfsPackage(File& file, size_t expectedFileLength,
                        const uint8_t* expectedPayloadDigest,
                        const char* expectedDesignId,
                        FaceModel& output,
                        PackageValidationError& error);
const char* packageValidationErrorName(PackageValidationError error);
