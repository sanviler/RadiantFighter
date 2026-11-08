#pragma once
// Rob Northen ProPack, method 1 ("RNC\x01"). Used for MAIN.BIN and the entries of the .epf archives.
#include "formats/common.h"

namespace dm::rnc {

bool isPacked(const uint8_t* data, size_t size);
uint16_t crc16(const uint8_t* data, size_t size);

// Unpacks; throws FormatError if the header is invalid or the data CRC does not match.
Bytes unpack(const uint8_t* data, size_t size);
inline Bytes unpack(const Bytes& b) { return unpack(b.data(), b.size()); }

} // namespace dm::rnc
