// Binary serialization helpers for the UDP game protocol (little endian).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../common/math.h"

namespace si {

class ByteWriter {
public:
    std::vector<uint8_t> buf;
    void u8(uint8_t v) { buf.push_back(v); }
    void u16(uint16_t v) { raw(&v, 2); }
    void u32(uint32_t v) { raw(&v, 4); }
    void i8(int8_t v) { raw(&v, 1); }
    void i16(int16_t v) { raw(&v, 2); }
    void i32(int32_t v) { raw(&v, 4); }
    void f32(float v) { raw(&v, 4); }
    void vec3(const Vec3& v) { f32(v.x); f32(v.y); f32(v.z); }
    // Compact vec3 with 1cm precision within +-327m of an origin is not enough for our
    // 1.5km map, so positions are sent as floats; directions use this helper.
    void dir(const Vec3& d) { i16((int16_t)(clampf(d.x, -1, 1) * 32767)); i16((int16_t)(clampf(d.y, -1, 1) * 32767)); i16((int16_t)(clampf(d.z, -1, 1) * 32767)); }
    void angle(float a) { i16((int16_t)(wrapAngle(a) / kPi * 32767.0f)); }
    void str(const std::string& s) {
        uint16_t n = (uint16_t)std::min<size_t>(s.size(), 60000);
        u16(n);
        raw(s.data(), n);
    }
    void bytes(const std::vector<uint8_t>& b) { buf.insert(buf.end(), b.begin(), b.end()); }
    void raw(const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        buf.insert(buf.end(), b, b + n);
    }
    size_t size() const { return buf.size(); }
    // Patch a previously written u16 (for counts written before contents).
    void patchU16(size_t at, uint16_t v) { std::memcpy(&buf[at], &v, 2); }
};

class ByteReader {
public:
    const uint8_t* p;
    size_t n, pos = 0;
    bool fail = false;
    ByteReader(const uint8_t* data, size_t len) : p(data), n(len) {}
    bool ok() const { return !fail; }
    size_t remaining() const { return fail ? 0 : n - pos; }
    uint8_t u8() { uint8_t v = 0; raw(&v, 1); return v; }
    uint16_t u16() { uint16_t v = 0; raw(&v, 2); return v; }
    uint32_t u32() { uint32_t v = 0; raw(&v, 4); return v; }
    int8_t i8() { int8_t v = 0; raw(&v, 1); return v; }
    int16_t i16() { int16_t v = 0; raw(&v, 2); return v; }
    int32_t i32() { int32_t v = 0; raw(&v, 4); return v; }
    float f32() { float v = 0; raw(&v, 4); return v; }
    Vec3 vec3() { Vec3 v; v.x = f32(); v.y = f32(); v.z = f32(); return v; }
    Vec3 dir() { Vec3 v; v.x = i16() / 32767.0f; v.y = i16() / 32767.0f; v.z = i16() / 32767.0f; return v; }
    float angle() { return i16() / 32767.0f * kPi; }
    std::string str() {
        uint16_t len = u16();
        if (fail || pos + len > n) { fail = true; return {}; }
        std::string s((const char*)p + pos, len);
        pos += len;
        return s;
    }
    void raw(void* out, size_t len) {
        if (fail || pos + len > n) { fail = true; std::memset(out, 0, len); return; }
        std::memcpy(out, p + pos, len);
        pos += len;
    }
    void skip(size_t len) { if (pos + len > n) fail = true; else pos += len; }
};

} // namespace si
