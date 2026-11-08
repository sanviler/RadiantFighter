#pragma once
// Amiga OCS palettes (12 bits, $0RGB) converted to 8-bit RGB.
#include "formats/common.h"
#include <array>

namespace dm {

struct Rgb { uint8_t r, g, b; };

struct Palette {
    std::array<Rgb, 256> c{};
    int count = 0;
    static Palette fromOcs(const uint8_t* words, int n);          // n word big-endian $0RGB
    static Palette fromCmap(const uint8_t* rgb, int n);           // chunk CMAP IFF
};

// Table of 16-colour palettes in the hunk of MAIN.BIN (offsets from the start of the code).
// $17F52 is the first; rooms select a palette through a pointer (see $08D36).
constexpr uint32_t kMainPaletteTable = 0x17F52;
constexpr uint32_t kMainPaletteStride = 32;

} // namespace dm
