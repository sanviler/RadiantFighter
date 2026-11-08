// Game loader: starts Shadow Fighter (AGA) on the machine without Kickstart and without disk drives.
//
// Shadow Fighter takes over the whole Amiga and reads its own disk format (logical tracks of $1600 bytes).
// This file does in C++ what an installer does, written from scratch:
//   - the main program (Disk.1 offset $73C00, RNC packed) is unpacked to $80500 and started at $80508;
//   - the track loader ($81AC0 / $81ACE: d0 track, d1 tracks, a0 destination, disk = word $8CEB0 + 1)
//     copies from the disk images;
//   - the CD32 path stays off (flag $8C768 = 0);
//   - the manual protection ($8AB54) gets the answer the game expects;
//   - the intro is read to $192000 instead of $1B2000: its 58 tracks would go past the end of the 2 MB of
//     chip RAM (on the real Amiga the last part of the buffer is lost; the packed data fit anyway);
//   - the intro can be skipped (option), as in the WHDLoad install when fire is held;
//   - the check code in the stage modules (lea x(pc),a4 / clr.w (a4) / lea y(pc),a5) is cut before it runs.
#include "amiga/cpu.h"
#include "amiga/machine.h"
#include "formats/rnc1.h"
#include "rf/disks.h"
#include "rf/options.h"
#include "rf/cdaudio.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace amiga {

namespace {
constexpr uint32_t kMainBase = 0x80500, kMainEntry = 0x80508, kMainDiskOffset = 0x73C00, kMainPacked = 0x169E9;
constexpr uint32_t kDiskWord = 0x8CEB0, kCdFlag = 0x8C768;
// high scores: 360 bytes, given to the selection module at $8C912 and kept by it at $11B33A; saved when the
// selection screen is left ($117A36). File format of the WHDLoad install: byte i XOR ($167 - i).
constexpr uint32_t kHighMain = 0x8C912, kHighSelect = 0x11B33A, kHighSize = 0x168, kHighSaveAt = 0x117A36;
constexpr const char* kHighFile = "RadiantFighter.highs";
constexpr const char* kHighFileWhd = "ShadowFighter.highs";

void scramble(std::vector<uint8_t>& d) { for (size_t i = 0; i < d.size(); ++i) d[i] ^= uint8_t(0x167 - i); }

// a valid table: printable characters, line feeds and $FF markers (the disk table has 9 line feeds)
bool highValid(const std::vector<uint8_t>& d) {
    if (d.size() != kHighSize) return false;
    int lf = 0;
    for (uint8_t c : d) { if (c == '\n') ++lf; else if (c && (c < 32 || c > 126) && c != 0xFF) return false; }
    return lf >= 9;
}

void patch16(Machine& m, uint32_t addr, uint16_t expect, uint16_t value) {
    if (m.peek16(addr) == expect) m.poke16(addr, value);
    else if (m.peek16(addr) == value) {}                          // already done (cracked ADF)
    else if (m.log) { char b[96]; std::snprintf(b, sizeof b, "patch skipped at $%X: $%04X", addr, m.peek16(addr)); m.log(b); }
}

void trackLoader(Machine& m) {
    Cpu& c = m.cpu();
    const int disk = int(m.peek16(kDiskWord)) + 1;
    const uint32_t track = uint32_t(c.getD(0) & 0xFFFF), count = uint32_t(c.getD(1) & 0xFFFF), dst = uint32_t(c.getA(0)) & 0xFFFFFF;
    const uint32_t size = count * rf::kTrack;
    const uint32_t room = dst < m.chipSize() ? m.chipSize() - dst : 0;
    std::vector<uint8_t> buf(size);
    const bool ok = rf::Disks::instance().read(disk, track * rf::kTrack, size, buf.data());
    for (uint32_t i = 0; i < size && i < room; ++i) m.chip()[dst + i] = buf[i];
    if (std::getenv("RF_LOG_READS") && m.log) {
        char b[120]; std::snprintf(b, sizeof b, "read disk %d track %u x%u -> $%X (frame %llu)%s", disk, track, count, dst,
                                   static_cast<unsigned long long>(m.frameCount()), ok ? "" : " OUTSIDE");
        m.log(b);
    }
    m.returnFromSubroutine();
}
} // namespace

bool Machine::boot(const std::string& saveDir, std::string& err) {
    saveDir_ = saveDir;
    auto& disks = rf::Disks::instance();
    if (!disks.ready()) { err = "Shadow Fighter disks not loaded"; return false; }
    setAga(true);
    std::fill(chip_.begin(), chip_.end(), 0);
    const auto& d1 = disks.image(1);
    dm::Bytes main;
    try { main = dm::rnc::unpack(d1.data() + kMainDiskOffset, kMainPacked); }
    catch (...) { err = "Disk.1: main program not valid"; return false; }
    for (size_t i = 0; i < main.size(); ++i) chip_[kMainBase + i] = main[i];
    installGameHooks(kMainBase);
    if (onGamePatched) onGamePatched(*this);
    // port directions as left by the Kickstart
    ciaA_.ddra = 0x03;
    ciaB_.ddra = 0xC0;
    ciaB_.ddrb = 0xFF;
    customWrite(0x100, 0);
    Cpu& c = *cpu_;
    c.reset();
    c.setSR(0x2000);                    // supervisor mode, interrupts enabled (the game never lowers the mask itself)
    c.setA(7, 0x7FFF0);
    jumpTo(kMainEntry);
    lineStart_ = c.getClock();
    gameBase_ = kMainBase;
    return true;
}

void Machine::installGameHooks(uint32_t) {
    patch16(*this, kCdFlag, 0x0001, 0x0000);                 // disk version, not CD32
    patch16(*this, 0x8C8F2, 0x0001, 0x0000);
    patch16(*this, 0x805CC, 0x001B, 0x0019);                 // intro buffer $192000 (load)
    patch16(*this, 0x805E4, 0x001B, 0x0019);                 // intro buffer $192000 (unpack)
    addHook(0x81AC0, trackLoader);
    addHook(0x81ACE, trackLoader);
    // high scores: the file next to the executable (or the WHDLoad one), else the table of the disk
    for (const char* name : {kHighFile, kHighFileWhd}) {
        std::ifstream f(fs::path(saveDir_) / name, std::ios::binary);
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
        if (d.size() != kHighSize) continue;
        scramble(d);
        if (!highValid(d)) { if (log) log(std::string(name) + ": not a high score table"); continue; }
        for (uint32_t i = 0; i < kHighSize; ++i) poke8(kHighMain + i, d[i]);
        break;
    }
    addHook(kHighSaveAt, [](Machine& m) {
        if (m.peek16(kHighSaveAt) == 0x4279 && m.peek32(kHighSaveAt + 2) == 0x00118E00 && !m.saveDir_.empty()) {
            std::vector<uint8_t> d(kHighSize);
            for (uint32_t i = 0; i < kHighSize; ++i) d[i] = m.peek8(kHighSelect + i);
            if (highValid(d)) {
                scramble(d);
                std::error_code ec;
                const fs::path p = fs::path(m.saveDir_) / kHighFile, tmp = fs::path(m.saveDir_) / (std::string(kHighFile) + ".tmp");
                { std::ofstream f(tmp, std::ios::binary); f.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size())); }
                fs::rename(tmp, p, ec);
                if (ec) { std::ofstream f(p, std::ios::binary); f.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size())); }
            }
        }
        m.cpu().execute();
    });
    // sound: the music comes either from the CD tracks or from Paula (option, never both).
    //  CD:    as on the CD32: menus track 3 (already when the menu music would start: title $8061C, $80660), each fight the
    //         track of its music number ($80C7C), the ending track 13 ($82B7E); SOUND = BACKGROUND forced
    //         ($80C52/$81D44) so that the fight keeps the stage sounds and the effects; the Paula music of the
    //         menus (main replayer $8B138 with an even music number, e.g. 42), of the selection ($118782) and of
    //         the ending ($1176D0) is skipped. The intro keeps its own music (the CD32 intro has no CD track).
    //  Paula: the original game, untouched (its SOUND option works).
    // Volumes: the channels written while a music replayer runs are in group 1 (music), the others in group 0
    // (effects and stage sounds); main.cpp sets the gain of each group.
    auto cdOn = [] { return rf::CdAudio::instance().enabled(); };
    for (uint32_t a : {0x80C52u, 0x81D44u}) addHook(a, [cdOn](Machine& m) { if (cdOn()) m.poke16(0x8C8E0, 1); m.cpu().execute(); });
    addHook(0x80C7C, [](Machine& m) { rf::CdAudio::instance().requestMusic(int(m.cpu().getD(4) & 0xFFFF)); m.cpu().execute(); });
    addHook(0x8061C, [](Machine& m) { rf::CdAudio::instance().requestTrack(3); m.cpu().execute(); });   // title screen
    addHook(0x80660, [](Machine& m) { rf::CdAudio::instance().requestTrack(3); m.cpu().execute(); });
    addHook(0x80770, [](Machine& m) { rf::CdAudio::instance().requestTrack(3); m.cpu().execute(); });
    addHook(0x82B7E, [](Machine& m) { rf::CdAudio::instance().requestTrack(13); m.cpu().execute(); });
    // (the tracks are always followed, so that switching to CD in the options plays the right one)
    // main replayer (music and stage sounds): even music number = music, odd = stage sounds (effects group)
    addHook(0x8B138, [cdOn](Machine& m) {
        const bool music = (m.peek16(0x8CEAC) & 1) == 0;
        if (music && cdOn()) { m.returnFromSubroutine(); return; }
        m.setAudioGroup(music ? 1 : 0);
        m.cpu().execute();
    });
    // module replayers (selection $118782, ending $1176D0, intro $1092DC): music group, skipped with CD except the intro
    for (uint32_t a : {0x118782u, 0x1176D0u, 0x1092DCu}) addHook(a, [a, cdOn](Machine& m) {
        const bool replayer = m.peek16(a) == 0x7C00 && m.peek32(a + 2) == 0x49F900DFu;
        if (replayer && cdOn() && a != 0x1092DCu) { m.returnFromSubroutine(); return; }
        if (replayer) m.setAudioGroup(1);
        m.cpu().execute();
    });
    // CIA-B timer of the main replayer (fade volume of channels 0 and 1): same group as the replayer, then back
    static int savedGroup = 0;
    addHook(0x8B520, [](Machine& m) { savedGroup = m.audioGroup(); m.setAudioGroup((m.peek16(0x8CEAC) & 1) == 0 ? 1 : 0); m.cpu().execute(); });
    addHook(0x8B57A, [](Machine& m) { m.setAudioGroup(savedGroup); m.cpu().execute(); });
    // back from the replayers: effects group again
    for (uint32_t a : {0xAB902u, 0xAB936u, 0x119D52u, 0x117F48u, 0x108F64u})
        addHook(a, [](Machine& m) { m.setAudioGroup(0); m.cpu().execute(); });
    // intro skip (option): from the intro loading straight to the code after "jsr $108000"
    if (custom1_ & rf::kSkipIntro) addHook(0x805CA, [](Machine& m) { m.jumpTo(0x805F8); });
    // the RNC unpacker ($8192C: a0 packed data with "RNC\1" or "DSKn", a1 destination, all registers kept) runs
    // in C++: the 68000 version took seconds for the big blocks (the black screen before the Gremlin logo)
    addHook(0x8192C, [](Machine& m) {
        Cpu& c = m.cpu();
        const uint32_t src = uint32_t(c.getA(0)) & 0xFFFFFF, dst = uint32_t(c.getA(1)) & 0xFFFFFF;
        const uint32_t packed = m.peek32(src + 8), size = 18 + packed;
        std::vector<uint8_t> buf(size);
        for (uint32_t i = 0; i < size; ++i) buf[i] = m.peek8(src + i);
        buf[0] = 'R'; buf[1] = 'N'; buf[2] = 'C'; buf[3] = 1;
        dm::Bytes out;
        try { out = dm::rnc::unpack(buf.data(), buf.size()); } catch (...) { out.clear(); }
        if (out.empty()) { if (m.log) m.log("RNC block not valid: original unpacker"); c.execute(); return; }
        for (size_t i = 0; i < out.size(); ++i) m.poke8(dst + uint32_t(i), out[i]);
        m.cpu().setClock(m.cpu().getClock() + int64_t(out.size()) / 8);   // a little time, not seconds
        m.returnFromSubroutine();
    });
    // manual protection: the game expects $12345678 at $18
    addHook(0x8AB54, [](Machine& m) { m.poke32(0x18, 0x12345678); m.returnFromSubroutine(); });
    // stage modules: cut the check before "jsr $120000"
    addHook(0x80E7A, [](Machine& m) {
        if (!std::getenv("RF_NO_STAGEPATCH"))
        for (uint32_t a = 0x120000; a < 0x121000; a += 2)
            if (m.peek16(a) == 0x49FA && m.peek32(a + 4) == 0x42544BFA) { m.poke16(a + 6, 0x4E75); if (m.log) { char b[64]; std::snprintf(b, sizeof b, "stage check cut at $%X", a + 6); m.log(b); } }
        const uint32_t sp = uint32_t(m.cpu().getA(7)) - 4;      // jsr $120000, back at $80E80
        m.cpu().setA(7, sp); m.poke32(sp, 0x80E80); m.jumpTo(0x120000);
    });
}

} // namespace amiga
