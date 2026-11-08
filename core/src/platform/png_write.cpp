#include "platform/png_write.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace dm {

namespace {

uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    static std::array<uint32_t, 256> t = [] {
        std::array<uint32_t, 256> r{};
        for (uint32_t i = 0; i < 256; ++i) { uint32_t v = i; for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1; r[i] = v; }
        return r;
    }();
    for (size_t i = 0; i < n; ++i) c = t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

struct BitWriter {
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    void put(uint32_t v, int n) {                       // n bits, least significant first
        acc |= v << bits; bits += n;
        while (bits >= 8) { out.push_back(uint8_t(acc)); acc >>= 8; bits -= 8; }
    }
    void putRev(uint32_t code, int n) {                 // Huffman code, most significant first
        uint32_t r = 0;
        for (int i = 0; i < n; ++i) r |= ((code >> i) & 1) << (n - 1 - i);
        put(r, n);
    }
    void flush() { if (bits) { out.push_back(uint8_t(acc)); acc = 0; bits = 0; } }
};

void putLiteral(BitWriter& bw, int v) {                 // fixed codes (RFC 1951, 3.2.6)
    if (v < 144) bw.putRev(uint32_t(0x30 + v), 8);
    else if (v < 256) bw.putRev(uint32_t(0x190 + v - 144), 9);
    else if (v < 280) bw.putRev(uint32_t(v - 256), 7);
    else bw.putRev(uint32_t(0xC0 + v - 280), 8);
}

const int kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void putMatch(BitWriter& bw, int len, int dist) {
    int i = 28;
    while (kLenBase[i] > len) --i;
    putLiteral(bw, 257 + i);
    if (kLenExtra[i]) bw.put(uint32_t(len - kLenBase[i]), kLenExtra[i]);
    int d = 29;
    while (kDistBase[d] > dist) --d;
    bw.putRev(uint32_t(d), 5);
    if (kDistExtra[d]) bw.put(uint32_t(dist - kDistBase[d]), kDistExtra[d]);
}

std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& in) {
    BitWriter bw;
    bw.out.push_back(0x78); bw.out.push_back(0x01);
    bw.put(1, 1); bw.put(1, 2);                          // a single final block, fixed Huffman
    constexpr int kWin = 32768, kHash = 1 << 15, kChain = 48;
    std::vector<int> head(kHash, -1), prev(in.size(), -1);
    auto hash = [&](size_t i) { return int(((uint32_t(in[i]) << 10) ^ (uint32_t(in[i + 1]) << 5) ^ in[i + 2]) & (kHash - 1)); };
    auto insert = [&](size_t i) { if (i + 2 < in.size()) { const int h = hash(i); prev[i] = head[size_t(h)]; head[size_t(h)] = int(i); } };
    size_t i = 0;
    while (i < in.size()) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < in.size()) {
            const size_t maxLen = std::min<size_t>(258, in.size() - i);
            int cand = head[size_t(hash(i))];
            for (int n = 0; cand >= 0 && n < kChain && int(i) - cand <= kWin; ++n, cand = prev[size_t(cand)]) {
                size_t l = 0;
                while (l < maxLen && in[size_t(cand) + l] == in[i + l]) ++l;
                if (int(l) > bestLen) { bestLen = int(l); bestDist = int(i) - cand; if (l == maxLen) break; }
            }
        }
        if (bestLen >= 3) {
            putMatch(bw, bestLen, bestDist);
            for (int k = 0; k < bestLen; ++k) insert(i + size_t(k));
            i += size_t(bestLen);
        } else { putLiteral(bw, in[i]); insert(i); ++i; }
    }
    putLiteral(bw, 256);
    bw.flush();
    uint32_t a = 1, b = 0;
    for (uint8_t v : in) { a = (a + v) % 65521; b = (b + a) % 65521; }
    const uint32_t ad = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) bw.out.push_back(uint8_t(ad >> s));
    return bw.out;
}

void chunk(std::FILE* f, const char* type, const std::vector<uint8_t>& data) {
    const uint32_t n = uint32_t(data.size());
    const uint8_t len[4] = {uint8_t(n >> 24), uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
    std::fwrite(len, 1, 4, f);
    std::fwrite(type, 1, 4, f);
    if (n) std::fwrite(data.data(), 1, n, f);
    uint32_t c = crc32(reinterpret_cast<const uint8_t*>(type), 4);
    c = crc32(data.data(), data.size(), c) ^ 0xFFFFFFFFu;
    const uint8_t cb[4] = {uint8_t(c >> 24), uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c)};
    std::fwrite(cb, 1, 4, f);
}

} // namespace

bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    if (w <= 0 || h <= 0 || rgb.size() < size_t(w) * size_t(h) * 3) return false;
    // filters: for every row the one with the lowest sum of absolute values (None, Sub, Up)
    const size_t stride = size_t(w) * 3;
    std::vector<uint8_t> raw;
    raw.reserve((stride + 1) * size_t(h));
    std::vector<uint8_t> cand[3];
    for (auto& c : cand) c.resize(stride);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = &rgb[size_t(y) * stride];
        const uint8_t* up = y ? row - stride : nullptr;
        long best = -1; int bf = 0;
        for (int fi = 0; fi < 3; ++fi) {
            long sum = 0;
            for (size_t x = 0; x < stride; ++x) {
                const int left = x >= 3 ? row[x - 3] : 0, above = up ? up[x] : 0;
                const uint8_t v = uint8_t(row[x] - (fi == 1 ? left : fi == 2 ? above : 0));
                cand[fi][x] = v;
                sum += v < 128 ? v : 256 - v;
            }
            if (best < 0 || sum < best) { best = sum; bf = fi; }
        }
        raw.push_back(uint8_t(bf == 0 ? 0 : bf == 1 ? 1 : 2));
        raw.insert(raw.end(), cand[bf].begin(), cand[bf].end());
    }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr = {uint8_t(w >> 24), uint8_t(w >> 16), uint8_t(w >> 8), uint8_t(w),
                                 uint8_t(h >> 24), uint8_t(h >> 16), uint8_t(h >> 8), uint8_t(h), 8, 2, 0, 0, 0};
    chunk(f, "IHDR", ihdr);
    chunk(f, "IDAT", zlibCompress(raw));
    chunk(f, "IEND", {});
    return std::fclose(f) == 0;
}

} // namespace dm
