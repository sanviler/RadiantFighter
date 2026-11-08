#pragma once
// The picture shown in the window, taken from the machine's frame (hires, 2 pixels per lores pixel).
//   - Full-screen pictures (title, menus, selection): the PAL overscan area the game uses, colour clocks
//     $71-$1D1 (352 lores pixels, the selection screen) and lines $22-$131 (272 lines), 4:3 as on the Amiga.
//   - The fight: exactly its display window (DIWSTRT/DIWSTOP, $3381-$1FBD: 316x236), so that it fills the
//     height of the window or screen (the black above and below is not part of the fight picture).
// The window scales the picture until its height reaches the edges (aspect ratio kept).
#include "amiga/machine.h"
#include "formats/palette.h"
#include <vector>

namespace rf {

constexpr int kViewX = 0x71, kViewY = 0x22, kViewW = 352, kViewH = 272;

struct ViewRect { int x, y, w, h; };

// the fight screen: 8 bitplanes (BPLCON0 $0410)
inline bool fightScreen(const amiga::Machine& m) { return (m.firstBplCon0 & 0x7010) == 0x0010; }

inline ViewRect viewRect(const amiga::Machine& m) {
    if (fightScreen(m)) {
        const uint16_t s = m.customReg(0x08E), e = m.customReg(0x090);
        const int hs = s & 0xFF, vs = s >> 8, he = (e & 0xFF) | 0x100, ve = (e >> 8) | ((e & 0x8000) ? 0 : 0x100);
        if (he - hs >= 256 && ve - vs >= 160 && hs >= 0x50 && ve <= 0x138) return {hs, vs, he - hs, ve - vs};
    }
    return {kViewX, kViewY, kViewW, kViewH};
}

inline void copyView(const amiga::Machine& m, std::vector<dm::Rgb>& out, const ViewRect& r) {
    const auto& fb = m.framebuffer();
    out.resize(size_t(r.w) * r.h);
    const int x0 = r.x - amiga::Machine::kFbFirstLores, y0 = r.y - amiga::Machine::kFbFirstLine;
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) {
            const int sx = (x0 + x) * 2, sy = y0 + y;
            uint32_t c = 0;
            if (sx >= 0 && sy >= 0 && sx < amiga::Machine::kFbWidth && sy < amiga::Machine::kFbHeight)
                c = fb[size_t(sy) * amiga::Machine::kFbWidth + size_t(sx)];
            out[size_t(y) * r.w + x] = dm::Rgb{uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c)};
        }
}

} // namespace rf
