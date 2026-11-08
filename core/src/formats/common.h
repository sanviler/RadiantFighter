#pragma once
// Common types and helpers for the Darkmere file formats (big-endian Amiga data).
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <stdexcept>

namespace dm {

using Bytes = std::vector<uint8_t>;

inline uint16_t be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
inline uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Bytes readFile(const std::string& path);
void writeFile(const std::string& path, const Bytes& data);

} // namespace dm
