#include "formats/adf.h"
#include <algorithm>

namespace dm::adf {
namespace {
class Reader {
public:
    explicit Reader(const Bytes& img) : img_(img) {}
    bool valid() const { return img_.size() >= 512 * 1760 && img_[0] == 'D' && img_[1] == 'O' && img_[2] == 'S' && img_[3] <= 7; }
    bool ffs() const { return img_[3] & 1; }
    uint32_t root() const { return uint32_t(img_.size() / 512 / 2); }   // root block in the middle of the disk

    void list(uint32_t dirBlock, const std::string& prefix, std::map<std::string, Bytes>& out, int depth = 0) const {
        if (depth > 8) return;
        for (int i = 6; i < 6 + 72; ++i) {
            uint32_t h = L(dirBlock, i);
            for (int guard = 0; h && guard < 4096; ++guard) {
                if (!inRange(h)) break;
                const int32_t type = int32_t(L(h, 127));
                if (type == 2) list(h, prefix + name(h) + "/", out, depth + 1);   // directory
                else if (type == -3) out[prefix + name(h)] = file(h);               // file
                h = L(h, 124);                                                      // next in hash chain
            }
        }
    }

private:
    const Bytes& img_;
    bool inRange(uint32_t b) const { return size_t(b + 1) * 512 <= img_.size(); }
    uint32_t L(uint32_t block, int index) const { return be32(&img_[size_t(block) * 512 + size_t(index) * 4]); }
    std::string name(uint32_t block) const {
        const uint8_t* p = &img_[size_t(block) * 512 + 432];
        return std::string(reinterpret_cast<const char*>(p + 1), std::min<size_t>(p[0], 30));
    }
    Bytes file(uint32_t header) const {
        const uint32_t size = L(header, 81);
        std::vector<uint32_t> blocks;
        for (uint32_t cur = header; cur && inRange(cur) && blocks.size() < 4096;) {
            const uint32_t n = std::min<uint32_t>(L(cur, 2), 72);
            for (uint32_t k = 0; k < n; ++k) blocks.push_back(L(cur, 77 - int(k)));   // table is filled backwards
            cur = L(cur, 126);                                                         // extension block
        }
        Bytes out;
        out.reserve(size);
        for (uint32_t b : blocks) {
            if (!inRange(b)) break;
            const uint8_t* p = &img_[size_t(b) * 512];
            if (ffs()) out.insert(out.end(), p, p + 512);
            else out.insert(out.end(), p + 24, p + 24 + std::min<uint32_t>(be32(p + 12), 488));   // OFS data block
        }
        if (out.size() > size) out.resize(size);
        return out;
    }
};
} // namespace

bool listFiles(const Bytes& image, std::map<std::string, Bytes>& out, std::string& err) {
    Reader r(image);
    if (!r.valid()) { err = "not an AmigaDOS disk image (custom trackloader?)"; return false; }
    r.list(r.root(), "", out);
    return true;
}

Bytes readSectors(const Bytes& image, size_t offset, size_t length) {
    if (offset >= image.size()) return {};
    const size_t end = std::min(image.size(), offset + length);
    return Bytes(image.begin() + long(offset), image.begin() + long(end));
}

} // namespace dm::adf
