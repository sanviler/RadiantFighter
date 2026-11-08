#pragma once
// SHA-256 (FIPS 180-4), used to check the user's game files.
#include <cstddef>
#include <cstdint>
#include <string>

namespace dm {

std::string sha256Hex(const uint8_t* data, size_t size);   // lower-case hex digest

} // namespace dm
