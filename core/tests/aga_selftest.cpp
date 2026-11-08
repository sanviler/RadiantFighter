// AGA self-test for amiga::Machine: expected pixels computed independently from the register semantics.
#include "amiga/cpu.h"
#include "amiga/machine.h"
#include "platform/png_write.h"
#include <cstdio>
#include <vector>
using namespace amiga;
bool Machine::boot(const std::string&, std::string&) { return true; }
void Machine::installGameHooks(uint32_t) {}
static uint32_t pal24(int i) { return uint32_t((i * 37 + 11) & 255) << 16 | uint32_t((i * 91 + 7) & 255) << 8 | uint32_t((i * 53 + 201) & 255); }
int main(int argc, char** argv) {
    Machine m; m.setAga(true);
    m.poke16(0x1000, 0x60FE);
    m.cpu().reset(); m.cpu().setSR(0x2700); m.cpu().setA(7, 0x7FFF0); m.jumpTo(0x1000);
    const int W = 320, H = 200, BPR = 40;
    const uint32_t bpl = 0x180000;                        // above 1.5 MB: needs 2 MB chip
    auto value = [&](int x, int y) { return (x + y) & 255; };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const int v = value(x, y);
            for (int p = 0; p < 8; ++p)
                if (v & (1 << p)) { uint32_t a = bpl + uint32_t(p) * BPR * H + uint32_t(y) * BPR + x / 8; m.poke8(a, uint8_t(m.peek8(a) | (0x80 >> (x & 7)))); }
        }
    uint32_t c = 0x70000;
    auto mv = [&](uint16_t r, uint16_t v) { m.poke16(c, r); m.poke16(c + 2, v); c += 4; };
    auto wt = [&](int v, int h) { m.poke16(c, uint16_t(((v & 0xFF) << 8) | (h & 0xFE) | 1)); m.poke16(c + 2, 0xFFFE); c += 4; };
    for (int bank = 0; bank < 8; ++bank) {
        mv(0x106, uint16_t(bank << 13));
        for (int i = 0; i < 32; ++i) { uint32_t p = pal24(bank * 32 + i); mv(uint16_t(0x180 + 2 * i), uint16_t(((p >> 20) & 15) << 8 | ((p >> 12) & 15) << 4 | ((p >> 4) & 15))); }
        mv(0x106, uint16_t(bank << 13 | 0x200));
        for (int i = 0; i < 32; ++i) { uint32_t p = pal24(bank * 32 + i); mv(uint16_t(0x180 + 2 * i), uint16_t(((p >> 16) & 15) << 8 | ((p >> 8) & 15) << 4 | (p & 15))); }
    }
    mv(0x106, 0);
    mv(0x1FC, 3);                                          // FMODE: 64-bit bitplane fetch
    mv(0x08E, 0x2C81); mv(0x090, 0xF4C1); mv(0x092, 0x0038); mv(0x094, 0x00A0);   // 4x: 320 pixels = DDF $38-$A0 (5 units)
    mv(0x108, 0); mv(0x10A, 0); mv(0x102, 0); mv(0x104, 0);
    for (int p = 0; p < 8; ++p) { uint32_t pt = bpl + uint32_t(p) * BPR * H; mv(uint16_t(0xE0 + p * 4), uint16_t(pt >> 16)); mv(uint16_t(0xE2 + p * 4), uint16_t(pt)); }
    mv(0x100, 0x0210);                                     // 8 planes (BPU3), ECSENA
    // bands: lines 0-49 normal, 50-99 BPLAM=$A5, 100-149 HAM8, 150-199 6 planes EHB then KILLEHB
    wt(0x2C + 50, 0x07); mv(0x10C, 0xA511);
    wt(0x2C + 100, 0x07); mv(0x10C, 0x0011); mv(0x100, 0x0A10);
    wt(0x2C + 150, 0x07); mv(0x100, 0x6200);
    wt(0x2C + 175, 0x07); mv(0x104, 0x0200);
    m.poke32(c, 0xFFFFFFFE);
    m.busWrite16(0xDFF080, 0x0007); m.busWrite16(0xDFF082, 0x0000); m.busWrite16(0xDFF088, 0);
    m.busWrite16(0xDFF096, 0x83C0);
    for (int f = 0; f < 2; ++f) m.runFrame();
    const auto& fb = m.framebuffer();
    // display window starts at lores $81: framebuffer x = (0x81 - kFbFirstLores) * 2, line 0x2C
    const int fx0 = (0x81 - Machine::kFbFirstLores) * 2, fy0 = 0x2C - Machine::kFbFirstLine;
    long bad[5] = {}, tot[5] = {};
    for (int y = 0; y < H; ++y) {
        uint32_t ham = 0;
        for (int x = 0; x < W; ++x) {
            int v = value(x, y), band; uint32_t exp;
            if (y < 50) { band = 0; exp = pal24(v); }
            else if (y < 100) { band = 1; exp = pal24(v ^ 0xA5); }
            else if (y < 150) {
                band = 2; const int d = v >> 2;
                if (x == 0) ham = pal24(0);
                switch (v & 3) {
                    case 0: ham = pal24(d); break;
                    case 1: ham = (ham & 0xFFFF00) | uint32_t(d << 2 | (ham & 3)); break;
                    case 2: ham = (ham & 0x00FFFF) | uint32_t(d << 2 | ((ham >> 16) & 3)) << 16; break;
                    default: ham = (ham & 0xFF00FF) | uint32_t(d << 2 | ((ham >> 8) & 3)) << 8; break;
                }
                exp = ham;
            } else {
                const int v6 = v & 63;
                if (y < 175) { band = 3; exp = (v6 & 32) ? (pal24(v6 & 31) >> 1) & 0x7F7F7F : pal24(v6); }
                else { band = 4; exp = pal24(v6); }
            }
            const uint32_t got = fb[size_t(fy0 + y) * Machine::kFbWidth + fx0 + 2 * x];
            ++tot[band]; if (got != exp) ++bad[band];
        }
    }
    const char* nm[5] = {"8 planes FMODE3 + palette BANK/LOCT", "BPLAM", "HAM8", "EHB 6 planes", "KILLEHB"};
    int fail = 0;
    for (int b = 0; b < 5; ++b) { std::printf("%-36s %6ld/%6ld wrong\n", nm[b], bad[b], tot[b]); fail |= bad[b] != 0; }
    if (argc > 1) { std::vector<uint8_t> rgb; for (uint32_t p : fb) { rgb.push_back(uint8_t(p >> 16)); rgb.push_back(uint8_t(p >> 8)); rgb.push_back(uint8_t(p)); } dm::writePng(argv[1], Machine::kFbWidth, Machine::kFbHeight, rgb); }
    return fail;
}
