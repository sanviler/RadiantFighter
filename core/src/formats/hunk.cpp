#include "formats/hunk.h"
#include <algorithm>

namespace dm::hunk {

bool load(const Bytes& d, uint32_t base, Loaded& out, std::string& err) {
    try {
        auto L = [&](size_t at) { return be32(&d.at(at)); };
        if (d.size() < 24 || L(0) != 0x3F3) { err = "not an AmigaDOS executable"; return false; }
        size_t o = 4;
        while (L(o)) o += 4 + L(o) * 4;           // resident library names
        o += 4;
        const uint32_t n = L(o);
        o += 12;                                  // table size, first, last
        out = Loaded{};
        out.start.resize(n); out.size.resize(n); out.memFlags.resize(n);
        uint32_t pos = 0;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t w = L(o); o += 4;
            out.size[i] = (w & 0x3FFFFFFF) * 4;
            out.memFlags[i] = w & 0xC0000000;
            if ((w & 0xC0000000) == 0xC0000000) o += 4;   // explicit memory attributes
            out.start[i] = pos; pos += (out.size[i] + 7) & ~7u;
        }
        out.image.assign(pos, 0);
        int h = -1;
        while (o + 4 <= d.size()) {
            const uint32_t t = L(o) & 0x3FFFFFFF; o += 4;
            if (t == 0x3E9 || t == 0x3EA) {       // code, data
                ++h; const uint32_t len = L(o) * 4; o += 4;
                std::copy(d.begin() + long(o), d.begin() + long(o + len), out.image.begin() + out.start.at(size_t(h)));
                o += len;
            } else if (t == 0x3EB) { ++h; o += 4; }   // bss
            else if (t == 0x3EC) {                // relocations (32 bit)
                for (;;) {
                    const uint32_t cnt = L(o); o += 4;
                    if (!cnt) break;
                    const uint32_t target = L(o); o += 4;
                    for (uint32_t k = 0; k < cnt; ++k, o += 4) {
                        const uint32_t at = out.start.at(size_t(h)) + L(o);
                        const uint32_t v = be32(&out.image.at(at + 3) - 3) + base + out.start.at(target);
                        out.image[at] = uint8_t(v >> 24); out.image[at + 1] = uint8_t(v >> 16);
                        out.image[at + 2] = uint8_t(v >> 8); out.image[at + 3] = uint8_t(v);
                        ++out.relocations;
                    }
                }
            } else if (t == 0x3F2) {              // end of hunk
            } else if (t == 0x3F0) {              // symbols: (name length, name, value)... 0
                while (const uint32_t len = L(o)) o += 4 + len * 4 + 4;
                o += 4;
            } else if (t == 0x3F1) { o += 4 + L(o) * 4; }   // debug
            else if (t == 0x3F7 || t == 0x3F8) { err = "short relocations (HUNK_DREL) not supported"; return false; }
            else break;
        }
        return true;
    } catch (const std::exception&) {
        err = "truncated executable";
        return false;
    }
}

} // namespace dm::hunk
