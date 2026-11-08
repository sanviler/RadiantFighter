// OCS regression test for amiga::Machine: copper-driven bands with every OCS display mode (1-6 planes, hires, EHB,
// HAM, dual playfield, sprites, attached sprites). The SHA-256 of the framebuffer must not change when the chipset
// code changes (AGA mode off). Usage: ocs_regress [ocs|aga] [out.png]
#include "amiga/cpu.h"
#include "amiga/machine.h"
#include "platform/png_write.h"
#include "platform/sha256.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace amiga;
bool Machine::boot(const std::string&, std::string&) { return true; }
void Machine::installGameHooks(uint32_t) {}
static uint32_t rng = 12345;
static uint16_t rnd() { rng = rng * 1103515245 + 12345; return uint16_t(rng >> 16); }
int main(int argc, char** argv) {
    bool aga = argc > 1 && std::strcmp(argv[1], "aga") == 0;
    Machine m;
    if (aga) m.setAga(true);
    // CPU: endless loop at $1000
    m.poke16(0x1000, 0x60FE);
    m.cpu().reset(); m.cpu().setSR(0x2700); m.cpu().setA(7, 0x7FFF0); m.jumpTo(0x1000);
    // bitplane data: 8 planes x 40 bytes x 256 lines at $10000 (interleaved by plane blocks)
    for (uint32_t a = 0x10000; a < 0x10000 + 8 * 40 * 256 + 4096; a += 2) m.poke16(a, rnd());
    // sprite 0..7 data at $60000
    for (int s = 0; s < 8; ++s) {
        uint32_t a = 0x60000 + s * 0x200;
        int vs = 60 + s * 20, ve = vs + 40, hs = 0x60 + s * 24;
        m.poke16(a, uint16_t((vs & 0xFF) << 8 | ((hs >> 1) & 0xFF)));
        m.poke16(a + 2, uint16_t((ve & 0xFF) << 8 | ((vs >> 8) & 1) << 2 | ((ve >> 8) & 1) << 1 | (hs & 1) | ((s == 3) ? 0x80 : 0)));
        for (int l = 0; l < 40; ++l) { m.poke16(a + 4 + l * 4, rnd()); m.poke16(a + 6 + l * 4, rnd()); }
        m.poke32(a + 4 + 40 * 4, 0);
    }
    // copper list at $70000
    uint32_t c = 0x70000;
    auto mv = [&](uint16_t r, uint16_t v) { m.poke16(c, r); m.poke16(c + 2, v); c += 4; };
    auto wt = [&](int v, int h) { m.poke16(c, uint16_t(((v & 0xFF) << 8) | (h & 0xFE) | 1)); m.poke16(c + 2, 0xFFFE); c += 4; };
    for (int i = 0; i < 32; ++i) mv(uint16_t(0x180 + 2 * i), uint16_t(rnd() & 0xFFF));
    mv(0x08E, 0x2C81); mv(0x090, 0x2CC1); mv(0x092, 0x0038); mv(0x094, 0x00D0);
    mv(0x108, 0); mv(0x10A, 0); mv(0x102, 0x0021); mv(0x104, 0x0024);
    for (int s = 0; s < 8; ++s) { mv(uint16_t(0x120 + s * 4), 0x0006); mv(uint16_t(0x122 + s * 4), uint16_t(s * 0x200)); }
    const uint16_t modes[] = {0x1200, 0x2200, 0x3200, 0x4200, 0x5200, 0x6200 /*EHB*/, 0x6A00 /*HAM*/, 0x6600 /*DPF*/,
                              0x9200, 0xC200 /*hires 4*/, 0x5200, 0x6600};
    for (int b = 0; b < 12; ++b) {
        int v = 0x2C + b * 21;
        wt(v, 0x07);
        for (int p = 0; p < 6; ++p) { uint32_t pt = 0x10000 + p * 40 * 256 + b * 21 * 40; mv(uint16_t(0xE0 + p * 4), uint16_t(pt >> 16)); mv(uint16_t(0xE2 + p * 4), uint16_t(pt)); }
        mv(0x100, modes[b]);
        if (modes[b] & 0x8000) { mv(0x092, 0x003C); mv(0x094, 0x00D4); } else { mv(0x092, 0x0038); mv(0x094, 0x00D0); }
        wt(v + 8, 0x41); mv(0x180, uint16_t(rnd() & 0xFFF)); mv(0x184, uint16_t(rnd() & 0xFFF));
        wt(v + 8, 0x91); mv(0x182, uint16_t(rnd() & 0xFFF));
    }
    m.poke32(c, 0xFFFFFFFE);
    m.poke16(0x0DFF080 & 0, 0);
    m.busWrite16(0xDFF080, 0x0007); m.busWrite16(0xDFF082, 0x0000);
    m.busWrite16(0xDFF088, 0);
    m.busWrite16(0xDFF096, 0x83E0);
    for (int f = 0; f < 3; ++f) m.runFrame();
    const auto& fb = m.framebuffer();
    std::vector<uint8_t> bytes(fb.size() * 4);
    std::memcpy(bytes.data(), fb.data(), bytes.size());
    std::vector<uint8_t> rgb; for (uint32_t p : fb) { rgb.push_back(uint8_t(p >> 16)); rgb.push_back(uint8_t(p >> 8)); rgb.push_back(uint8_t(p)); }
    if (argc > 2) dm::writePng(argv[2], Machine::kFbWidth, Machine::kFbHeight, rgb);
    const std::string h = dm::sha256Hex(bytes.data(), bytes.size());
    static const char* kExpected = "de27074c48af76b6858b82b025d28b4f3ea0d783fb430e53dc67bbb3d4d0835f";   // OCS picture (framebuffer from lores $50; the area from $70 is the same as before AGA)
    std::printf("%s %s\n", h.c_str(), aga ? "(AGA mode)" : (h == kExpected ? "OK" : "DIFFERENT"));
    return (!aga && h != kExpected) ? 1 : 0;
}
