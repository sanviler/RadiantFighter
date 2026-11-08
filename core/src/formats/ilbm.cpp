#include "formats/ilbm.h"
#include <cstring>

namespace dm {

IlbmImage decodeIlbm(const Bytes& d) {
    if (d.size() < 12 || std::memcmp(d.data(), "FORM", 4) || std::memcmp(d.data() + 8, "ILBM", 4))
        throw FormatError("ILBM: not a FORM ILBM");
    IlbmImage img;
    int masking = 0, compression = 0;
    const uint8_t* body = nullptr;
    size_t bodyLen = 0;
    size_t o = 12;
    while (o + 8 <= d.size()) {
        const uint8_t* id = &d[o];
        uint32_t n = be32(&d[o + 4]);
        const uint8_t* c = &d[o + 8];
        if (o + 8 + n > d.size()) break;
        if (!std::memcmp(id, "BMHD", 4)) {
            img.width = be16(c); img.height = be16(c + 2);
            img.planes = c[8]; masking = c[9]; compression = c[10];
        } else if (!std::memcmp(id, "CMAP", 4)) {
            img.palette = Palette::fromCmap(c, int(n / 3));
        } else if (!std::memcmp(id, "CAMG", 4)) {
            img.ham = (be32(c) & 0x800) != 0;
        } else if (!std::memcmp(id, "BODY", 4)) {
            body = c; bodyLen = n;
        }
        o += 8 + n + (n & 1);
    }
    if (!body || !img.width) throw FormatError("ILBM: BMHD/BODY mancanti");

    const int rowBytes = ((img.width + 15) / 16) * 2;
    const int planesInFile = img.planes + (masking == 1 ? 1 : 0);
    const size_t need = size_t(rowBytes) * planesInFile * img.height;
    Bytes raw;
    raw.reserve(need);
    if (compression == 1) {
        size_t i = 0;
        while (i < bodyLen && raw.size() < need) {
            int8_t n = int8_t(body[i++]);
            if (n >= 0) {
                for (int k = 0; k <= n && i < bodyLen; ++k) raw.push_back(body[i++]);
            } else if (n != -128 && i < bodyLen) {
                raw.insert(raw.end(), size_t(1 - n), body[i++]);
            }
        }
    } else {
        raw.assign(body, body + std::min(bodyLen, need));
    }
    raw.resize(need, 0);

    img.indices.assign(size_t(img.width) * img.height, 0);
    for (int y = 0; y < img.height; ++y)
        for (int p = 0; p < img.planes; ++p) {
            const uint8_t* row = &raw[(size_t(y) * planesInFile + p) * rowBytes];
            for (int x = 0; x < img.width; ++x)
                if (row[x >> 3] & (0x80 >> (x & 7))) img.indices[size_t(y) * img.width + x] |= uint8_t(1 << p);
        }
    return img;
}

std::vector<Rgb> IlbmImage::toRgb() const {
    std::vector<Rgb> out(indices.size());
    for (int y = 0; y < height; ++y) {
        Rgb cur = palette.c[0];
        for (int x = 0; x < width; ++x) {
            uint8_t v = indices[size_t(y) * width + x];
            if (!ham) { out[size_t(y) * width + x] = palette.c[v]; continue; }
            uint8_t ctl = v >> 4, val = uint8_t((v & 15) * 17);
            switch (ctl) {
                case 0: cur = palette.c[v & 15]; break;
                case 1: cur.b = val; break;
                case 2: cur.r = val; break;
                default: cur.g = val; break;
            }
            out[size_t(y) * width + x] = cur;
        }
    }
    return out;
}

} // namespace dm
