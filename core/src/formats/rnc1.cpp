#include "formats/rnc1.h"
#include <array>
#include <fstream>

namespace dm {

Bytes readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw FormatError("cannot open " + path);
    return Bytes(std::istreambuf_iterator<char>(f), {});
}

void writeFile(const std::string& path, const Bytes& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw FormatError("cannot write " + path);
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

} // namespace dm

namespace dm::rnc {

namespace {

struct HuffEntry { uint32_t len, code, sym; };

class BitStream {
public:
    BitStream(const uint8_t* d, size_t n, size_t pos) : d_(d), n_(n), p_(pos) {}
    void init() { buf_ = word(); p_ += 2; cnt_ = 16; }
    uint32_t read(uint32_t n) {
        uint32_t v = buf_ & ((1u << n) - 1);
        buf_ >>= n; cnt_ -= int(n);
        if (cnt_ < 16) { buf_ |= word() << cnt_; p_ += 2; cnt_ += 16; }
        return v;
    }
    uint32_t peek() const { return buf_; }
    // After a block of literals the bit stream must be realigned (the RNC1 format
    // keeps 16 bits ahead in the buffer: literals are read from p_-2).
    void copyLiterals(Bytes& out, uint32_t n) {
        int r = cnt_ - 16;
        size_t q = p_ - 2;
        if (q + n > n_) throw FormatError("RNC1: literals past the end");
        out.insert(out.end(), d_ + q, d_ + q + n);
        p_ = q + n;
        buf_ &= (1u << r) - 1;
        buf_ |= word() << r;
        p_ += 2; cnt_ = r + 16;
    }
private:
    uint32_t word() const {
        uint32_t lo = p_ < n_ ? d_[p_] : 0, hi = p_ + 1 < n_ ? d_[p_ + 1] : 0;
        return lo | (hi << 8);
    }
    const uint8_t* d_; size_t n_; size_t p_;
    uint32_t buf_ = 0; int cnt_ = 0;
};

uint32_t mirror(uint32_t v, uint32_t n) {
    uint32_t r = 0;
    for (uint32_t i = 0; i < n; ++i) { r = (r << 1) | (v & 1); v >>= 1; }
    return r;
}

std::vector<HuffEntry> makeTable(BitStream& bs) {
    std::vector<HuffEntry> tab;
    uint32_t n = bs.read(5);
    if (!n) return tab;
    if (n > 16) n = 16;
    std::array<uint32_t, 16> len{};
    for (uint32_t i = 0; i < n; ++i) len[i] = bs.read(4);
    uint32_t code = 0;
    for (uint32_t L = 1; L <= 16; ++L) {
        for (uint32_t s = 0; s < n; ++s)
            if (len[s] == L) tab.push_back({L, mirror(code, L), s}), ++code;
        code <<= 1;
    }
    return tab;
}

uint32_t readHuff(BitStream& bs, const std::vector<HuffEntry>& tab) {
    for (const auto& e : tab) {
        if ((bs.peek() & ((1u << e.len) - 1)) == e.code) {
            bs.read(e.len);
            uint32_t s = e.sym;
            if (s >= 2) { uint32_t x = s - 1; s = (1u << x) | bs.read(x); }
            return s;
        }
    }
    throw FormatError("RNC1: invalid Huffman code");
}

} // namespace

bool isPacked(const uint8_t* d, size_t n) {
    return n >= 18 && d[0] == 'R' && d[1] == 'N' && d[2] == 'C' && d[3] == 1;
}

uint16_t crc16(const uint8_t* d, size_t n) {
    static const auto table = [] {
        std::array<uint16_t, 256> t{};
        for (int i = 0; i < 256; ++i) {
            uint16_t c = uint16_t(i);
            for (int k = 0; k < 8; ++k) c = (c & 1) ? uint16_t((c >> 1) ^ 0xA001) : uint16_t(c >> 1);
            t[i] = c;
        }
        return t;
    }();
    uint16_t c = 0;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c;
}

Bytes unpack(const uint8_t* d, size_t n) {
    if (!isPacked(d, n)) throw FormatError("RNC1: header missing");
    const uint32_t usize = be32(d + 4);
    const uint16_t ucrc = be16(d + 12);
    Bytes out;
    out.reserve(usize);
    BitStream bs(d, n, 18);
    bs.init();
    bs.read(2); // flags (encrypted block / unused)
    while (out.size() < usize) {
        auto raw = makeTable(bs), dist = makeTable(bs), len = makeTable(bs);
        uint32_t count = bs.read(16);
        while (count--) {
            uint32_t lit = readHuff(bs, raw);
            if (lit) bs.copyLiterals(out, lit);
            if (count) {
                uint32_t pos = readHuff(bs, dist) + 1;
                uint32_t l = readHuff(bs, len) + 2;
                if (pos > out.size()) throw FormatError("RNC1: invalid distance");
                for (uint32_t i = 0; i < l; ++i) out.push_back(out[out.size() - pos]);
            }
        }
    }
    out.resize(usize);
    if (crc16(out.data(), out.size()) != ucrc) throw FormatError("RNC1: wrong CRC");
    return out;
}

} // namespace dm::rnc
