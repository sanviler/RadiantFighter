#include "formats/palette.h"

namespace dm {

Palette Palette::fromOcs(const uint8_t* w, int n) {
    Palette p;
    p.count = n;
    for (int i = 0; i < n && i < 256; ++i) {
        uint16_t v = be16(w + i * 2);
        p.c[i] = {uint8_t(((v >> 8) & 15) * 17), uint8_t(((v >> 4) & 15) * 17), uint8_t((v & 15) * 17)};
    }
    return p;
}

Palette Palette::fromCmap(const uint8_t* rgb, int n) {
    Palette p;
    p.count = n;
    for (int i = 0; i < n && i < 256; ++i) p.c[i] = {rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]};
    return p;
}

} // namespace dm
