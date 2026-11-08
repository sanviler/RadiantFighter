#pragma once
// AmigaDOS executables (hunk format): every hunk loaded one after the other (8-byte aligned) and relocated for
// a load address. Extracted from Lightmere's loader. Packed executables (RNC, Imploder, PowerPacker...) must be
// unpacked first.
#include "formats/common.h"
#include <string>
#include <vector>

namespace dm::hunk {

struct Loaded {
    Bytes image;                        // all hunks, BSS zero-filled
    std::vector<uint32_t> start;        // offset of each hunk in the image
    std::vector<uint32_t> size;         // size of each hunk (bytes)
    std::vector<uint32_t> memFlags;     // top bits of the size words (chip/fast requests)
    size_t relocations = 0;
};

bool load(const Bytes& file, uint32_t base, Loaded& out, std::string& err);

} // namespace dm::hunk
