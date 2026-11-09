#pragma once

#include <stddef.h>
#include <stdint.h>
#include <algorithm>
#include <cstring>

namespace fs {
enum SeekMode { SeekSet = 0 };

class File {
public:
    File() = default;
    File(const uint8_t* data, size_t length)
        : data_(data), length_(length), position_(0), open_(data != nullptr) {}

    explicit operator bool() const { return open_; }
    size_t size() const { return length_; }

    bool seek(size_t position, SeekMode) {
        if (!open_ || position > length_) return false;
        position_ = position;
        return true;
    }

    size_t read(uint8_t* output, size_t requested) {
        if (!open_ || output == nullptr || position_ > length_) return 0;
        const size_t available = length_ - position_;
        const size_t count = std::min(available, requested);
        if (count > 0) std::memcpy(output, data_ + position_, count);
        position_ += count;
        return count;
    }

    void close() { open_ = false; }

private:
    const uint8_t* data_ = nullptr;
    size_t length_ = 0;
    size_t position_ = 0;
    bool open_ = false;
};
} // namespace fs
