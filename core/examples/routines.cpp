#include "game/routines.h"
#include "amiga/cpu.h"
#include <cstdio>
#include <vector>

namespace dm::game {

namespace {

// ---------------------------------------------------------------------------------------------
// $0A3EE: largest free block.
// In: d0.w = 0 -> pool $1F6B0, otherwise pool $1F6AC (if it exists, otherwise $1F6B0).
// List nodes: +0 next, +4 previous, +8 size (long), +12 used (word, 0 = free).
// Out: a0 = largest free block (the last one on a tie; the head if there are none),
//      d0 = its size, d1 = total free. d2, d7, a1, a2 unchanged. CCR as after "cmpa.l #0,a1".
// It is slow in the game because it walks the whole list, which grows as memory fragments.
// ---------------------------------------------------------------------------------------------
struct FreeResult { uint32_t a0, d0, d1; };

FreeResult largestFree(const amiga::Machine& m, uint16_t sel) {
    const uint32_t base = m.gameBase();
    uint32_t a1 = m.peek32(base + 0x1F6B0);
    const uint32_t chip = m.peek32(base + 0x1F6AC);
    if (chip && sel != 0) a1 = chip;
    FreeResult r{a1, 0, 0};
    for (int guard = 0; a1 && guard < 1000000; ++guard) {
        if (m.peek16(a1 + 12) == 0) {
            const uint32_t sz = m.peek32(a1 + 8);
            r.d1 += sz;
            if (!(r.d0 > sz)) { r.d0 = sz; r.a0 = a1; }
        }
        a1 = m.peek32(a1);
    }
    return r;
}

// runs the original code until the CPU reaches "stopPc" with the stack at "stopSp" (0 = any)
void runOriginal(amiga::Machine& m, uint32_t stopPc, uint32_t stopSp) {
    auto& c = m.cpu();
    for (long guard = 0; guard < 50'000'000; ++guard) {
        if ((c.getPC() & 0xFFFFFE) == stopPc && (!stopSp || uint32_t(c.getA(7)) == stopSp)) return;
        c.execute();
    }
}

// ---------------------------------------------------------------------------------------------
// $0AA92 (inside $0AA40): mirrored copy of a graphic. At the hook a0/a1 point to the
// source and destination descriptors, a6+d6 to the record of the source graphic (+8 width, +10 height).
// For every row of each of the 5 planes the bytes are written in reverse order, each with its bits
// reversed (table at $18E6E). Then the routine restores the registers ($0AAE0) and returns.
// ---------------------------------------------------------------------------------------------
} // namespace

std::string describe(const Routines& r) {
    char b[256];
    std::snprintf(b, sizeof b, "C++ routines: largest free block %llu calls (%llu differences), mirrored graphic %llu calls (%llu differences)",
                  static_cast<unsigned long long>(r.largestFreeBlock.calls), static_cast<unsigned long long>(r.largestFreeBlock.mismatches),
                  static_cast<unsigned long long>(r.mirrorGraphic.calls), static_cast<unsigned long long>(r.mirrorGraphic.mismatches));
    return b;
}

void installRoutines(amiga::Machine& m, HookMode mode, Routines& st) {
    if (mode == HookMode::Off) return;
    const uint32_t base = m.gameBase();

    // --- $0A3EE ---
    m.addHook(base + 0x0A3EE, [mode, &st](amiga::Machine& mm) {
        auto& c = mm.cpu();
        ++st.largestFreeBlock.calls;
        const FreeResult r = largestFree(mm, uint16_t(c.getD(0)));
        if (mode == HookMode::Replace) {
            c.setD(0, r.d0); c.setD(1, r.d1); c.setA(0, r.a0);
            c.setCCR(uint8_t((c.getCCR() & 0x10) | 0x04));
            mm.returnFromSubroutine();
            return;
        }
        const uint32_t sp = c.getA(7), ra = mm.peek32(sp);
        const uint32_t d2 = c.getD(2), d7 = c.getD(7), a1 = c.getA(1), a2 = c.getA(2);
        // the original code runs, skipping the hook (it starts at the hooked instruction)
        c.execute();
        runOriginal(mm, ra & 0xFFFFFE, sp + 4);
        const bool same = c.getD(0) == r.d0 && c.getD(1) == r.d1 && c.getA(0) == r.a0 &&
                          c.getD(2) == d2 && c.getD(7) == d7 && c.getA(1) == a1 && c.getA(2) == a2 &&
                          (c.getCCR() & 0x0F) == 0x04;
        if (!same) {
            ++st.largestFreeBlock.mismatches;
            if (mm.log) {
                char b[200];
                std::snprintf(b, sizeof b, "$0A3EE differs: C++ a0=%06X d0=%X d1=%X, original a0=%06X d0=%X d1=%X",
                              r.a0, r.d0, r.d1, unsigned(c.getA(0)), unsigned(c.getD(0)), unsigned(c.getD(1)));
                mm.log(b);
            }
        }
    });

    // --- $0AA92 ---
    m.addHook(base + 0x0AA92, [mode, &st, base](amiga::Machine& mm) {
        auto& c = mm.cpu();
        ++st.mirrorGraphic.calls;
        const uint32_t a6 = c.getA(6), d6 = c.getD(6);
        const uint32_t src = mm.peek32(c.getA(0)), dst = mm.peek32(c.getA(1));
        const uint32_t table = base + 0x18E6E;
        const uint32_t wbytes = ((uint32_t(mm.peek16(a6 + d6 + 8)) + 15) >> 4) * 2;
        const uint32_t rows = uint32_t(mm.peek16(a6 + d6 + 10)) * 5;
        std::vector<uint8_t> out(size_t(wbytes) * rows);
        for (uint32_t r = 0; r < rows; ++r)
            for (uint32_t i = 0; i < wbytes; ++i)
                out[size_t(r) * wbytes + (wbytes - 1 - i)] = mm.peek8(table + mm.peek8(src + r * wbytes + i));
        if (mode == HookMode::Replace) {
            for (size_t k = 0; k < out.size(); ++k) mm.poke8(dst + uint32_t(k), out[k]);
            mm.jumpTo(base + 0x0AAE0);
            return;
        }
        c.execute();
        runOriginal(mm, base + 0x0AAE0, 0);
        size_t diff = 0;
        for (size_t k = 0; k < out.size(); ++k) if (mm.peek8(dst + uint32_t(k)) != out[k]) ++diff;
        if (diff) {
            ++st.mirrorGraphic.mismatches;
            if (mm.log) mm.log("$0AA92 differs: " + std::to_string(diff) + " bytes of " + std::to_string(out.size()));
        }
    });
}

} // namespace dm::game
