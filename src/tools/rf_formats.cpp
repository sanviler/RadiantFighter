// rf_formats: converts Shadow Fighter blocks with the C++ decoders (checked against sf_formats.py / sf_mod.py).
// Usage: rf_formats exe.bin out_dir block.bin...   -> out_dir/<block>.mod, out_dir/<block>.parts (raw dump)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/sf_formats.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
static rf::Bytes load(const char* p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void save(const std::string& p, const rf::Bytes& b) { std::ofstream f(p, std::ios::binary); f.write((const char*)b.data(), b.size()); }
int main(int argc, char** argv) {
    if (argc < 4) { std::puts("rf_formats exe.bin out_dir block.bin..."); return 1; }
    auto exe = load(argv[1]); uint16_t per[37];
    for (int i = 0; i < 37; ++i) { size_t o = 0x8B20E - 0x80500 + 2 * i; per[i] = uint16_t(exe[o] << 8 | exe[o + 1]); }
    int nm = 0, nf = 0;
    for (int i = 3; i < argc; ++i) {
        auto d = load(argv[i]); std::string stem = argv[i];
        stem = stem.substr(stem.find_last_of("/\\") + 1); stem = stem.substr(0, stem.size() - 4);
        if (auto m = rf::parseMusic(d)) { save(std::string(argv[2]) + "/" + stem + ".mod", rf::musicToMod(d, *m, per, "SF " + stem)); ++nm; }
        else if (auto f = rf::parseFighter(d)) {
            rf::Bytes dump; std::vector<uint8_t> px, mk;
            for (auto& p : f->parts) if (p.bytesPerRow && p.height) { rf::decodePart(d, *f, p, px, mk); for (size_t k = 0; k < px.size(); ++k) dump.push_back(uint8_t(px[k] | mk[k] << 4)); }
            save(std::string(argv[2]) + "/" + stem + ".parts", dump); ++nf;
            std::printf("%s %s parti %zu fotogrammi %zu\n", stem.c_str(), f->name.c_str(), f->parts.size(), f->frames.size());
        }
    }
    std::printf("moduli %d lottatori %d\n", nm, nf);
    return 0;
}
