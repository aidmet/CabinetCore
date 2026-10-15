#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace cc {

inline uint32_t crc32(const uint8_t* data, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= data[i];
        for (int b = 0; b < 8; ++b) {
            uint32_t mask = 0u - (c & 1u);
            c = (c >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~c;
}

class ByteWriter {
public:
    void u8(uint8_t v) { buf_.push_back(v); }
    void u16(uint16_t v) {
        buf_.push_back(static_cast<uint8_t>(v & 255));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 255));
    }
    void u32(uint32_t v) {
        buf_.push_back(static_cast<uint8_t>(v & 255));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 255));
        buf_.push_back(static_cast<uint8_t>((v >> 16) & 255));
        buf_.push_back(static_cast<uint8_t>((v >> 24) & 255));
    }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v) {
        uint32_t bits = 0;
        static_assert(sizeof(float) == 4, "float must be 32-bit");
        std::memcpy(&bits, &v, 4);
        u32(bits);
    }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void str(const std::string& s) {
        u32(static_cast<uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }
    void bytes(const uint8_t* p, size_t n) { buf_.insert(buf_.end(), p, p + n); }
    void i16s(const std::vector<int16_t>& s) {
        u32(static_cast<uint32_t>(s.size()));
        for (int16_t v : s) u16(static_cast<uint16_t>(v));
    }
    const std::vector<uint8_t>& data() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool ok = true;

    uint8_t u8() {
        if (i_ + 1 > n_) {
            ok = false;
            return 0;
        }
        return p_[i_++];
    }
    uint16_t u16() {
        uint16_t v = u8();
        v |= static_cast<uint16_t>(u8()) << 8;
        return v;
    }
    uint32_t u32() {
        uint32_t v = u8();
        v |= static_cast<uint32_t>(u8()) << 8;
        v |= static_cast<uint32_t>(u8()) << 16;
        v |= static_cast<uint32_t>(u8()) << 24;
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    float f32() {
        uint32_t bits = u32();
        float v = 0.f;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    bool boolean() { return u8() != 0; }
    std::string str() {
        uint32_t n = u32();
        if (!ok || n > 1'000'000 || i_ + n > n_) {
            ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(p_ + i_), reinterpret_cast<const char*>(p_ + i_ + n));
        i_ += n;
        return s;
    }
    std::vector<uint8_t> bytes(uint32_t n) {
        if (!ok || n > 32'000'000 || i_ + n > n_) {
            ok = false;
            return {};
        }
        std::vector<uint8_t> out(p_ + i_, p_ + i_ + n);
        i_ += n;
        return out;
    }
    std::vector<int16_t> i16s() {
        uint32_t n = u32();
        if (!ok || n > 8'000'000 || i_ + n * 2 > n_) {
            ok = false;
            return {};
        }
        std::vector<int16_t> out(n);
        for (uint32_t k = 0; k < n; ++k) out[k] = static_cast<int16_t>(u16());
        return out;
    }
    size_t tell() const { return i_; }

private:
    const uint8_t* p_;
    size_t n_;
    size_t i_ = 0;
};

}  // namespace cc
