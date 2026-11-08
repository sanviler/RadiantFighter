#include "formats/planar.h"
#include <bit>

namespace dm {

PlanarImage decodePlanar(const Bytes& d, int ww, int h, int planes) {
    PlanarImage img;
    img.width = ww * 16;
    img.height = h;
    img.indices.assign(size_t(img.width) * h, 0);
    const size_t rb = size_t(ww) * 2, ps = rb * h;
    if (ps * planes > d.size()) throw FormatError("planar: not enough data");
    for (int p = 0; p < planes; ++p)
        for (int y = 0; y < h; ++y) {
            const uint8_t* row = &d[p * ps + y * rb];
            for (int x = 0; x < img.width; ++x)
                if (row[x >> 3] & (0x80 >> (x & 7))) img.indices[size_t(y) * img.width + x] |= uint8_t(1 << p);
        }
    return img;
}

int guessWidthWords(const Bytes& d, int planes) {
    double best = -1;
    int bestW = 0;
    for (int ww = 1; ww <= 20; ++ww) {
        size_t unit = size_t(ww) * 2 * planes;
        if (d.size() % unit) continue;
        int h = int(d.size() / unit);
        if (h < 4) continue;
        size_t rb = size_t(ww) * 2, ps = rb * h, same = 0, tot = 0;
        for (int p = 0; p < planes; ++p)
            for (int y = 0; y + 1 < h; ++y)
                for (size_t k = 0; k < rb; ++k) {
                    uint8_t a = d[p * ps + y * rb + k], b = d[p * ps + (y + 1) * rb + k];
                    same += 8 - std::popcount(unsigned(a ^ b));
                    tot += 8;
                }
        double s = tot ? double(same) / double(tot) : 0;
        if (s > best + 0.005) { best = s; bestW = ww; }
    }
    return bestW;
}

} // namespace dm
