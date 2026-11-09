#include "../../src/communication/digest.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main() {
    uint8_t digest[32];
    memset(digest, 0xA5, sizeof(digest));
    const char* lower =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    assert(WfsDigest::parseSha256Hex(lower, digest));
    const uint8_t expectedLower[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    for (size_t i = 0; i < 32; ++i) {
        assert(digest[i] == expectedLower[i % 8]);
    }

    const char* upper =
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789";
    assert(WfsDigest::parseSha256Hex(upper, digest));
    assert(digest[0] == 0xAB && digest[1] == 0xCD && digest[2] == 0xEF);

    uint8_t unchanged[32];
    memset(unchanged, 0x5A, sizeof(unchanged));
    memcpy(digest, unchanged, sizeof(digest));
    const char* invalid[] = {
        nullptr,
        "",
        "0123456789abcdef",
        " 123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "+123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg"
    };
    for (const char* value : invalid) {
        assert(!WfsDigest::parseSha256Hex(value, digest));
        assert(memcmp(digest, unchanged, sizeof(digest)) == 0);
    }
    assert(!WfsDigest::parseSha256Hex(lower, nullptr));
    return 0;
}
