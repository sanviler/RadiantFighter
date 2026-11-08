#include "formats/epf.h"
#include "formats/rnc1.h"

namespace dm {

EpfArchive::EpfArchive(Bytes data) : data_(std::move(data)) {
    if (data_.size() < 8 || data_[0] != 'E' || data_[1] != 'P' || data_[2] != 'F')
        throw FormatError("EPF: signature missing");
    const size_t n = data_[3];
    if (be32(&data_[4]) != data_.size()) throw FormatError("EPF: inconsistent size");
    if (8 + n * 8 > data_.size()) throw FormatError("EPF: truncated table");
    for (size_t i = 0; i < n; ++i) {
        Entry e{be32(&data_[8 + i * 8]), be32(&data_[12 + i * 8])};
        if (size_t(e.off) + e.len > data_.size()) throw FormatError("EPF: entry out of bounds");
        entries_.push_back(e);
    }
}

bool EpfArchive::isEmpty(size_t i) const {
    const auto& e = entries_.at(i);
    const uint8_t* p = data_.data() + e.off;
    return e.len <= 8 && e.len >= 4 && p[0] == 'R' && p[1] == 'N' && p[2] == 'C' && p[3] == 0;
}

Bytes EpfArchive::entry(size_t i) const {
    const auto& e = entries_.at(i);
    const uint8_t* p = data_.data() + e.off;
    if (isEmpty(i)) return {};
    if (rnc::isPacked(p, e.len)) return rnc::unpack(p, e.len);
    if (e.len >= 8 && p[0] == 'R' && p[1] == 'N' && p[2] == 'C' && p[3] == 0) {   // stored
        const uint32_t n = be32(p + 4);
        if (size_t(n) + 8 > e.len) throw FormatError("EPF: stored entry truncated");
        return Bytes(p + 8, p + 8 + n);
    }
    return Bytes(p, p + e.len);
}

Bytes EpfArchive::raw(size_t i) const {
    const auto& e = entries_.at(i);
    return Bytes(data_.begin() + e.off, data_.begin() + e.off + e.len);
}

namespace {
void putBe32(Bytes& b, size_t at, uint32_t v) {
    b[at] = uint8_t(v >> 24); b[at + 1] = uint8_t(v >> 16); b[at + 2] = uint8_t(v >> 8); b[at + 3] = uint8_t(v);
}
} // namespace

Bytes EpfArchive::build(const std::vector<Bytes>& raws) {
    if (raws.size() > 255) throw FormatError("EPF: too many entries");
    Bytes out(8 + raws.size() * 8, 0);
    out[0] = 'E'; out[1] = 'P'; out[2] = 'F'; out[3] = uint8_t(raws.size());
    for (size_t i = 0; i < raws.size(); ++i) {
        putBe32(out, 8 + i * 8, uint32_t(out.size()));
        putBe32(out, 12 + i * 8, uint32_t(raws[i].size()));
        out.insert(out.end(), raws[i].begin(), raws[i].end());
    }
    putBe32(out, 4, uint32_t(out.size()));
    return out;
}

Bytes EpfArchive::stored(const Bytes& data) {
    // the game's decruncher copies whole long words: a few bytes of padding keep the copy inside the entry
    Bytes out(8, 0);
    out[0] = 'R'; out[1] = 'N'; out[2] = 'C'; out[3] = 0;
    putBe32(out, 4, uint32_t(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    out.resize(out.size() + 4, 0);
    return out;
}

} // namespace dm
