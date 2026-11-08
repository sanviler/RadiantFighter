// Game loader: starts Darkmere on the machine without Kickstart and without disk drives.
//
// The game runs as a normal program that loads its files. This replaces what a WHDLoad slave does for the
// installed version, written from scratch in C++ (no slave code is used or needed):
//   - a minimal ExecBase at $1000 with AllocMem and AvailMem (the only system calls left in the game);
//   - MAIN.BIN (RNC packed, one hunk) loaded at the start of the expansion memory and relocated;
//   - the game's disk routines replaced by C++ hooks reading the original files (from the ADF images or the
//     WHDLoad data folder): file loader ($1B00), RNC decruncher ($18D8), save disk access ($23A4);
//   - the operating system calls (Forbid, Disable, dos.library) skipped, the disk copy protection bypassed;
//   - a few guards against reading through a null pointer;
//   - blitter waits completed at once (the emulated blitter finishes immediately).
// The optional cheats (infinite help, magic, energy) are kept for testing.
#include "amiga/machine.h"
#include "amiga/cpu.h"
#include "formats/game_files.h"
#include "formats/rnc1.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace amiga {

namespace {
uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

constexpr uint32_t kExecBase = 0x1000;
constexpr uint32_t kMainSize = 0x23300;            // memory reserved for MAIN.BIN
constexpr uint32_t kExpSize = 0x101000;            // expansion memory: program, its data, the stack on top
constexpr uint32_t kSaveDiskSize = 0x6F600;        // the save disk image (891 blocks)
// private area (not visible to the game): hook entry points and a zeroed block for the null pointer guards
constexpr uint32_t kAllocMemEntry = Machine::kPrivateBase + 0x100;
constexpr uint32_t kAvailMemEntry = Machine::kPrivateBase + 0x110;
constexpr uint32_t kNullBlock = Machine::kPrivateBase + 0x200;

bool readAll(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

// AmigaDOS executable (hunks) relocated to "base"
bool loadHunks(const std::vector<uint8_t>& d, std::vector<uint8_t>& image, uint32_t base, std::string& err) {
    auto L = [&](size_t at) { return be32(&d.at(at)); };
    if (d.size() < 24 || L(0) != 0x3F3) { err = "MAIN.BIN is not an AmigaDOS executable"; return false; }
    size_t o = 4;
    while (L(o)) o += 4 + L(o) * 4;               // resident library names (none)
    o += 4;
    const uint32_t n = L(o);
    o += 12;
    std::vector<uint32_t> start(n), sz(n);
    uint32_t pos = 0;
    for (uint32_t i = 0; i < n; ++i) {
        sz[i] = (L(o) & 0x3FFFFFFF) * 4; o += 4;
        start[i] = pos; pos += (sz[i] + 7) & ~7u;
    }
    image.assign(pos, 0);
    int h = -1;
    while (o + 4 <= d.size()) {
        const uint32_t t = L(o) & 0x3FFFFFFF; o += 4;
        if (t == 0x3E9 || t == 0x3EA) {           // code, data
            ++h; const uint32_t len = L(o) * 4; o += 4;
            std::copy(d.begin() + long(o), d.begin() + long(o + len), image.begin() + start[size_t(h)]);
            o += len;
        } else if (t == 0x3EB) { ++h; o += 4; }   // bss
        else if (t == 0x3EC) {                    // relocations
            for (;;) {
                const uint32_t cnt = L(o); o += 4;
                if (!cnt) break;
                const uint32_t target = L(o); o += 4;
                for (uint32_t k = 0; k < cnt; ++k, o += 4) {
                    const uint32_t at = start[size_t(h)] + L(o);
                    const uint32_t v = be32(&image[at]) + base + start[target];
                    image[at] = uint8_t(v >> 24); image[at + 1] = uint8_t(v >> 16); image[at + 2] = uint8_t(v >> 8); image[at + 3] = uint8_t(v);
                }
            }
        } else if (t == 0x3F2) {                  // end of hunk
        } else if (t == 0x3F0 || t == 0x3F1) { o += 4 + L(o) * 4; }
        else break;
    }
    return true;
}
} // namespace

// --- files ---
std::string Machine::readName(uint32_t a) const {
    std::string s;
    for (int i = 0; i < 255; ++i) { const char c = char(peek8(a + uint32_t(i))); if (!c) break; s += c; }
    return s;
}

std::string Machine::savePath(const std::string& name) const {
    return (fs::path(saveDir_) / name).string();
}

bool Machine::loadFileBytes(const std::string& name, std::vector<uint8_t>& out) {
    // saved files first (the save disk), then the original game files
    if (!saveDir_.empty() && readAll(savePath(name), out)) return true;
    try { out = dm::readGameFile(name); return true; } catch (const std::exception&) { return false; }
}

bool Machine::loadFilePart(const std::string& name, uint32_t size, uint32_t offset, uint32_t dest) {
    std::vector<uint8_t> d;
    if (!loadFileBytes(name, d) || offset + size > d.size()) return false;
    for (uint32_t k = 0; k < size; ++k) poke8(dest + k, d[offset + k]);
    return true;
}

bool Machine::saveFilePart(const std::string& name, uint32_t size, uint32_t offset, uint32_t src) {
    if (saveDir_.empty()) return false;
    std::error_code ec;
    fs::create_directories(saveDir_, ec);
    std::vector<uint8_t> d;
    if (!readAll(savePath(name), d)) loadFileBytes(name, d);   // first save: start from the original file, if any
    if (d.size() < offset + size) d.resize(offset + size, 0);
    for (uint32_t k = 0; k < size; ++k) d[offset + k] = peek8(src + k);
    std::ofstream f(savePath(name), std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
    return bool(f);
}

uint32_t Machine::decrunch(uint32_t src, uint32_t dst) {
    const uint8_t* p = mem(src, 18);
    if (!p || !(p[0] == 'R' && p[1] == 'N' && p[2] == 'C' && p[3] == 1)) return 0;
    const uint32_t packed = be32(p + 8) + 18;
    const uint8_t* q = mem(src, packed);
    if (!q) return 0;
    try {
        const std::vector<uint8_t> in(q, q + packed);
        const dm::Bytes out = dm::rnc::unpack(in.data(), in.size());
        uint8_t* w = mem(dst, uint32_t(out.size()));
        if (!w) return 0;
        std::copy(out.begin(), out.end(), w);
        return uint32_t(out.size());
    } catch (const std::exception& e) {
        if (log) log(std::string("decrunch: ") + e.what());
        return 0;
    }
}

// --- start ---
bool Machine::boot(const std::string& saveDir, std::string& err) {
    saveDir_ = saveDir;
    expSize_ = kExpSize;
    exp_.assign(expSize_, 0);
    Cpu& c = *cpu_;

    // ExecBase with AllocMem (-198) and AvailMem (-216); every other vector is invalid
    poke32(4, kExecBase);
    for (uint32_t a = kExecBase - 0x400; a < kExecBase; a += 4) poke32(a, 0xDEADC0DE);
    auto vector = [&](int offset, uint32_t entry) { poke16(kExecBase + uint32_t(offset), 0x4EF9); poke32(kExecBase + uint32_t(offset) + 2, entry); };
    vector(-198, kAllocMemEntry);
    vector(-216, kAvailMemEntry);
    pools_[0] = {kExecBase, kChipSize};                 // chip
    pools_[1] = {kExpBase, kExpBase + 0x100000};        // fast
    addHook(kAllocMemEntry, [](Machine& m) {
        Cpu& cc = m.cpu();
        uint32_t size = uint32_t(cc.getD(0));
        if (size & 3) size = (size & ~3u) + 4;
        const uint32_t flags = uint32_t(cc.getD(1)) & 6;
        int pool = (flags & 2) ? 0 : 1;
        for (;;) {
            Pool& p = m.pools_[pool];
            if (p.end - p.start >= size) { cc.setD(0, p.start); cc.setD(1, size); p.start += size; break; }
            if (flags == 0 && pool == 1) { pool = 0; continue; }   // any memory: fall back to chip
            cc.setD(0, 0); cc.setD(1, p.end - p.start); break;
        }
        m.returnFromSubroutine();
    });
    addHook(kAvailMemEntry, [](Machine& m) {
        const Pool& p = m.pools_[(uint32_t(m.cpu().getD(1)) & 2) ? 0 : 1];
        m.cpu().setD(0, p.end - p.start);
        m.returnFromSubroutine();
    });

    // the first 8 bytes of the save disk at $100, as at power on with the disk inserted
    loadFilePart("SaveDisk", 8, 0, 0x100);

    // MAIN.BIN
    std::vector<uint8_t> packed, image;
    try { packed = dm::readGameFile("MAIN.BIN"); } catch (const std::exception& e) { err = e.what(); return false; }
    dm::Bytes exe;
    try { exe = dm::rnc::unpack(packed.data(), packed.size()); } catch (const std::exception& e) { err = std::string("MAIN.BIN: ") + e.what(); return false; }
    const uint32_t base = pools_[1].start;
    pools_[1].start += kMainSize;
    if (!loadHunks(exe, image, base, err)) return false;
    if (image.size() > kMainSize) { err = "MAIN.BIN: unexpected size"; return false; }
    std::copy(image.begin(), image.end(), mem(base, uint32_t(image.size())));
    gameBase_ = base;
    installGameHooks(base);
    if (onGamePatched) onGamePatched(*this);

    customWrite(0x100, 0);                              // BPLCON0
    c.reset();
    c.setSR(0x2700);
    c.setA(7, kExpBase + kExpSize);
    c.setA(6, kExecBase);
    c.setA(3, base);
    jumpTo(base + 0x808);
    lineStart_ = c.getClock();
    return true;
}

void Machine::installGameHooks(uint32_t base) {
    auto D = [](Machine& m, int n) { return uint32_t(m.cpu().getD(n)); };
    auto A = [](Machine& m, int n) { return uint32_t(m.cpu().getA(n)); };

    // operating system calls skipped: Forbid/Disable at start-up, dos.library open and close
    poke16(base + 0x080E, 0x6000); poke16(base + 0x0810, 0x000A);   // bra $081A
    poke16(base + 0x9E50, 0x6000); poke16(base + 0x9E52, 0x004C);   // bra $9E9E
    poke8(base + 0x9FE8, 0x60);                                     // beq -> bra
    // disk copy protection (cracked disk sets already contain these same bytes)
    poke32(base + 0x8FAE, 0x600008C8);
    poke16(base + 0x98FE, 0x6012);
    poke16(base + 0x9952, 0x601E);

    // $18D8: decruncher. a0 = source, a1 = destination; d0 = unpacked size. "RNC" method 1 is unpacked in C++,
    // method 0 is a stored copy.
    addHook(base + 0x18D8, [D, A](Machine& m) {
        const uint32_t src = A(m, 0), dst = A(m, 1);
        if (m.peek32(src) == 0x524E4300) {
            const uint32_t len = m.peek32(src + 4);
            const uint32_t longs = (len + 3) / 4 + 1;
            for (uint32_t k = 0; k < longs; ++k) m.poke32(dst + k * 4, m.peek32(src + 8 + k * 4));
            m.cpu().setD(0, len);
        } else {
            m.cpu().setD(0, m.decrunch(src, dst));
        }
        (void)D;
        m.returnFromSubroutine();
    });

    // $1B00: disk routine. d0 = command; a0 + 4 = file name (after "DFx:"); a1 = buffer.
    //   0: load a whole file;  5: select a file for partial reads;  7: read the header (8 bytes) or the part
    //   announced by command 8;  8: read 8 bytes at offset d1, or announce a part;  6: nothing.
    // Other commands go on with the original code.
    addHook(base + 0x1B00, [base, D, A](Machine& m) {
        m.busWrite8(0xBFD100, 0x7F);                    // drive motors off
        switch (D(m, 0) & 0xFFFF) {
            case 5:
                m.selectedFile_ = m.readName(A(m, 0) + 4);
                m.cpu().setD(0, 1);
                break;
            case 7:
                if (D(m, 1) == m.announcedPart_) {
                    m.loadFilePart(m.selectedFile_, D(m, 1), m.peek32(base + 0xA5B6), A(m, 1));
                    m.poke32(base + 0xA5B6, 0);
                    m.poke32(base + 0xA5BA, 0);
                } else if (m.peek32(A(m, 1)) == 0 || m.peek16(A(m, 1)) == 0x4550) {
                    m.loadFilePart(m.selectedFile_, 8, 0, A(m, 1));
                }
                break;
            case 8:
                if (m.peek32(A(m, 1)) != D(m, 1)) m.loadFilePart(m.selectedFile_, D(m, 0), D(m, 1), A(m, 1));
                else m.announcedPart_ = m.peek32(A(m, 1) + 4);
                break;
            case 6:
                break;
            case 0: {
                std::vector<uint8_t> d;
                if (m.loadFileBytes(m.readName(A(m, 0) + 4), d)) {
                    if (uint8_t* w = m.mem(A(m, 1), uint32_t(d.size()))) std::copy(d.begin(), d.end(), w);
                } else if (m.log) m.log("file not found: " + m.readName(A(m, 0) + 4));
                m.cpu().setD(0, 0);
                break;
            }
            default:
                m.cpu().execute();                      // original code
                return;
        }
        m.returnFromSubroutine();
    });

    // $23A4: save disk, as a 456,192-byte image file "SaveDisk" in the save folder.
    // d3 = 0 read, 1 write, 2 format; d1 = first block, d2 = number of blocks, a0 = buffer.
    addHook(base + 0x23A4, [D, A](Machine& m) {
        const uint32_t cmd = D(m, 3) & 0xFFFF;
        const uint32_t size = (D(m, 2) & 0xFFFF) * 0x200, offset = (D(m, 1) & 0xFFFF) * 0x200;
        if (cmd == 1) m.saveFilePart("SaveDisk", size, offset, A(m, 0));
        else if (cmd == 2) { for (uint32_t o = 0; o < kSaveDiskSize; o += 0xA200) m.saveFilePart("SaveDisk", 0xA200, o, 0x200); }
        else if (size) m.loadFilePart("SaveDisk", size, offset, A(m, 0));
        m.cpu().setD(0, 0);
        m.returnFromSubroutine();
    });

    // guards: a negative pointer reads a block of zeros instead (8 bytes of original code replaced)
    auto guard = [](Machine& m, int reg) {
        const uint32_t p = uint32_t(m.cpu().getA(reg));
        return (p & 0x80000000u) ? kNullBlock : p;
    };
    addHook(base + 0xB232, [guard](Machine& m) {           // move.w 6(a2),d0 / move.w 10(a2),d1
        const uint32_t p = guard(m, 2);
        m.cpu().setA(2, p);
        m.cpu().setD(0, (uint32_t(m.cpu().getD(0)) & 0xFFFF0000u) | m.peek16(p + 6));
        m.cpu().setD(1, (uint32_t(m.cpu().getD(1)) & 0xFFFF0000u) | m.peek16(p + 10));
        m.jumpTo(uint32_t(m.cpu().getPC()) + 8);
    });
    addHook(base + 0xB258, [guard](Machine& m) {           // move.w 6(a4),d4 / move.w 10(a4),d5
        const uint32_t p = guard(m, 4);
        m.cpu().setA(4, p);
        m.cpu().setD(4, (uint32_t(m.cpu().getD(4)) & 0xFFFF0000u) | m.peek16(p + 6));
        m.cpu().setD(5, (uint32_t(m.cpu().getD(5)) & 0xFFFF0000u) | m.peek16(p + 10));
        m.jumpTo(uint32_t(m.cpu().getPC()) + 8);
    });
    addHook(base + 0xE158, [](Machine& m) {                // movea.l 2(a1),a2 / move.l 2(a2),d7
        uint32_t p = m.peek32(uint32_t(m.cpu().getA(1)) + 2);
        if (p & 0x80000000u) p = kNullBlock;
        m.cpu().setA(2, p);
        m.cpu().setD(7, m.peek32(p + 2));
        m.jumpTo(uint32_t(m.cpu().getPC()) + 8);
    });
    addHook(base + 0xE168, [](Machine& m) {                // movea.l 2(a0),a2 / move.l 2(a2),d7
        uint32_t p = m.peek32(uint32_t(m.cpu().getA(0)) + 2);
        if (p & 0x80000000u) p = kNullBlock;
        m.cpu().setA(2, p);
        m.cpu().setD(7, m.peek32(p + 2));
        m.jumpTo(uint32_t(m.cpu().getPC()) + 8);
    });

    // blitter waits (10 bytes each): the emulated blitter has already finished
    for (uint32_t off : {0x015D0u, 0x016D2u, 0x0185Au, 0x018AAu, 0x018C8u, 0x042C6u, 0x042FEu, 0x05686u, 0x0A9F0u, 0x0AA30u,
                         0x0AB2Eu, 0x0AB74u, 0x0AB98u, 0x0B00Eu, 0x0B03Au, 0x0B09Eu, 0x0B492u, 0x0B74Cu, 0x0C134u, 0x0C234u,
                         0x0C46Cu, 0x0CEA2u, 0x0CED8u, 0x0E416u})
        addHook(base + off, [](Machine& m) { m.jumpTo(uint32_t(m.cpu().getPC()) + 10); });

    // cheats for testing (bit 0 infinite help, bit 1 infinite magic, bit 2 infinite energy)
    if (custom1_ & 1) poke8(base + 0x4A1C, 0x4A);
    if (custom1_ & 2) poke8(base + 0x4A42, 0x4A);
    if (custom1_ & 4)
        addHook(base + 0x12DC, [A](Machine& m) {           // energy back to full
            m.poke16(A(m, 0) + 34, 0x0960);
            m.cpu().setD(0, m.peek8(A(m, 0) + 9));
            m.jumpTo(uint32_t(m.cpu().getPC()) + 6);
        });
}

} // namespace amiga
