// SPDX-License-Identifier: GPL-3.0-or-later
#include "sf_formats.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace rf {
namespace {
uint16_t be16(const Bytes& d, size_t o) { return uint16_t(d[o] << 8 | d[o + 1]); }
uint32_t be32(const Bytes& d, size_t o) { return uint32_t(be16(d, o)) << 16 | be16(d, o + 2); }
void put16(Bytes& b, uint16_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); }
}

std::optional<MusicModule> parseMusic(const Bytes& d) {
    if (d.size() < 24 || d[0] || d[2] || d[4]) return std::nullopt;
    MusicModule m{};
    for (int i = 0; i < 4; ++i) m.words[i] = be16(d, 2 * i);
    if (m.words[0] < 12) return std::nullopt;
    for (size_t o = 8; o + 4 < m.words[0]; o += 16)
        m.instruments.push_back({be16(d, o), be16(d, o + 6), be16(d, o + 12), be16(d, o + 14)});
    m.seq = m.words[0]; m.table = m.seq + m.words[1]; m.patterns = m.table + m.words[2]; m.samples = m.patterns + m.words[3];
    size_t total = m.samples;
    for (auto& i : m.instruments) total += 2u * i.lengthWords;
    if (m.instruments.empty() || total != d.size()) return std::nullopt;
    return m;
}

Bytes musicToMod(const Bytes& d, const MusicModule& m, const uint16_t per[37], const std::string& title) {
    int16_t len = int16_t(be16(d, m.seq - 4)), rs = int16_t(be16(d, m.seq - 2));
    std::vector<uint16_t> pos;
    for (int i = 0; i < len / 2; ++i) pos.push_back(be16(d, m.seq + 2 * i));
    std::set<uint16_t> uniq(pos.begin(), pos.end());
    std::map<uint16_t, uint8_t> idx; uint8_t k = 0;
    for (auto c : uniq) idx[c] = k++;
    Bytes out(20, 0);
    std::copy_n(title.begin(), std::min<size_t>(20, title.size()), out.begin());
    for (int i = 0; i < 31; ++i) {
        Bytes name(22, 0);
        if (i < int(m.instruments.size())) {
            std::string n = "ins" + std::string(1, char('0' + i / 10)) + char('0' + i % 10);
            std::copy(n.begin(), n.end(), name.begin());
            out.insert(out.end(), name.begin(), name.end());
            auto& in = m.instruments[i];
            put16(out, in.lengthWords); out.push_back(0); out.push_back(uint8_t(std::min<int>(in.volume, 64)));
            put16(out, in.loopStartWords); put16(out, in.loopWords ? in.loopWords : 1);
        } else {
            out.insert(out.end(), name.begin(), name.end());
            put16(out, 0); out.push_back(0); out.push_back(0); put16(out, 0); put16(out, 1);
        }
    }
    out.push_back(uint8_t(pos.size()));
    out.push_back(uint8_t((rs / 2 >= 0 && rs / 2 < int(pos.size())) ? rs / 2 : 0));
    for (int i = 0; i < 128; ++i) out.push_back(i < int(pos.size()) ? idx[pos[i]] : 0);
    out.insert(out.end(), {'M', '.', 'K', '.'});
    for (auto c : uniq) {
        uint8_t cells[4][64][3]; bool full[4][64] = {};
        for (int ch = 0; ch < 4; ++ch) {
            size_t p = m.patterns + be16(d, m.table + c + 2 * ch); int run = 0;
            for (int r = 0; r < 64; ++r) {
                if (run > 0) { --run; continue; }
                uint8_t b0 = d[p];
                if (b0 & 0x80) { run = 256 - b0 - 1; ++p; continue; }
                cells[ch][r][0] = b0; cells[ch][r][1] = d[p + 1]; cells[ch][r][2] = d[p + 2];
                full[ch][r] = true; p += 3;
            }
        }
        for (int r = 0; r < 64; ++r)
            for (int ch = 0; ch < 4; ++ch) {
                if (!full[ch][r]) { out.insert(out.end(), 4, 0); continue; }
                uint8_t b0 = cells[ch][r][0], b1 = cells[ch][r][1], b2 = cells[ch][r][2];
                int n = b0 & 0xFE; uint16_t period = n ? per[n / 2] : 0;
                int inst = ((b0 << 8 | b1) & 0x1F0) >> 4;
                out.push_back(uint8_t((inst & 0x10) | (period >> 8))); out.push_back(uint8_t(period));
                out.push_back(uint8_t(((inst & 0xF) << 4) | (b1 & 0xF))); out.push_back(b2);
            }
    }
    size_t s = m.samples;
    for (auto& in : m.instruments) { out.insert(out.end(), d.begin() + s, d.begin() + s + 2 * in.lengthWords); s += 2 * in.lengthWords; }
    return out;
}

std::optional<Fighter> parseFighter(const Bytes& d) {
    if (d.size() < 44) return std::nullopt;
    for (int i = 0; i < 8; ++i) if (d[i] < 32 || d[i] > 126) return std::nullopt;
    Fighter f{};
    for (int i = 0; i < 9; ++i) f.o[i] = be32(d, 8 + 4 * i);
    if (f.o[0] != 0x2C || f.o[8] != d.size() || f.o[5] + f.o[6] != d.size()) return std::nullopt;
    f.name.assign(d.begin(), d.begin() + 8);
    while (!f.name.empty() && f.name.back() == ' ') f.name.pop_back();
    size_t T = 0x2C + f.o[1];
    for (int i = 0; i < 256; ++i) {
        size_t o = T + 16 * i;
        f.parts.push_back({be32(d, o), be16(d, o + 4), be16(d, o + 6), be16(d, o + 8), be16(d, o + 10), be32(d, o + 12)});
    }
    f.gfxStart = f.o[4] + 768; f.gfxEnd = f.o[5];
    size_t F = 0x2C + f.o[2] + 4, p = F; std::vector<uint16_t> offs;
    for (;;) { uint16_t x = be16(d, p); if (!offs.empty() && x < offs.back()) break; offs.push_back(x); p += 2; }
    if (!offs.empty() && offs.back() == 0xFFFF) offs.pop_back();
    for (auto fo : offs) {
        std::vector<FramePiece> fr; size_t q = p + fo;
        while (q + 2 <= d.size()) {
            uint16_t s = be16(d, q); if (s == 0xFFFF) break;
            fr.push_back({s, int16_t(be16(d, q + 2)), int16_t(be16(d, q + 4))}); q += 6;
        }
        f.frames.push_back(std::move(fr));
    }
    return f;
}

void decodePart(const Bytes& d, const Fighter& f, const Part& p, std::vector<uint8_t>& pix, std::vector<uint8_t>& mask) {
    int w = p.bytesPerRow * 8; pix.assign(size_t(w) * p.height, 0); mask.assign(pix.size(), 0);
    const uint8_t* G = d.data() + f.gfxStart;
    for (int y = 0; y < p.height; ++y)
        for (int x = 0; x < w; ++x) {
            size_t b = size_t(y) * p.bytesPerRow + (x >> 3); uint8_t bit = uint8_t(0x80 >> (x & 7)); int c = 0;
            for (int pl = 0; pl < 4; ++pl) if (G[p.offset + size_t(pl) * p.planeSize + b] & bit) c |= 1 << pl;
            pix[size_t(y) * w + x] = uint8_t(c); mask[size_t(y) * w + x] = (G[p.maskOffset + b] & bit) ? 1 : 0;
        }
}

} // namespace rf
