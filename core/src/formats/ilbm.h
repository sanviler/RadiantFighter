#pragma once
// IFF ILBM (ByteRun1 or uncompressed), HAM6 included (CAMG bit $800).
// Used for screens (int4/out4/pic22/cor4/ded*) and level backgrounds (lev*_002, HAM6).
#include "formats/palette.h"

namespace dm {

struct IlbmImage {
    int width = 0, height = 0, planes = 0;
    bool ham = false;
    Palette palette;
    std::vector<uint8_t> indices;          // one index per pixel (planes bits)
    std::vector<Rgb> toRgb() const;        // resolves HAM or palette
};

IlbmImage decodeIlbm(const Bytes& data);

} // namespace dm
