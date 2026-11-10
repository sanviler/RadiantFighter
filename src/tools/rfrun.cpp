// rfrun: RadiantFighter without a window, for tests (developer tool, not shipped).
#include "rf/options.h"
// Usage: rfrun <folder with Disk.1-4> <frames> [--shot N file.png]... [--key N hexcode] [--joy N bits length]
//              [--custom1 N] [--wav file] [--dump N file] [--state N file]
// joystick bits (port 2): 1 up, 2 down, 4 left, 8 right, 16 fire; --joy1 for port 1 (set as joystick).
#include "amiga/cpu.h"
#include "amiga/machine.h"
#include "rf/cdaudio.h"
#include "rf/disks.h"
#include "rf/view.h"
#include "platform/png_write.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

static void savePng(const amiga::Machine& m, const std::string& path) {
    const auto& fb = m.framebuffer();
    std::vector<uint8_t> rgb;
    rgb.reserve(fb.size() * 3);
    for (uint32_t c : fb) { rgb.push_back(uint8_t(c >> 16)); rgb.push_back(uint8_t(c >> 8)); rgb.push_back(uint8_t(c)); }
    dm::writePng(path, amiga::Machine::kFbWidth, amiga::Machine::kFbHeight, rgb);
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: rfrun <folder> <frames> [options]\n"); return 1; }
    std::string msg;
    if (!rf::Disks::instance().load(argv[1], msg)) { std::fprintf(stderr, "%s\n", msg.c_str()); return 1; }
    std::fprintf(stderr, "%s\n", msg.c_str());
    amiga::Machine m;
    static uint32_t ring[256]; static unsigned ri = 0; static bool dumped = false;
    static amiga::Machine* mp = &m;
    if (std::getenv("RF_TRACE")) m.onStep = [](amiga::Machine& mm) { const uint32_t pc = uint32_t(mm.cpu().getPC()); if (ring[(ri - 1) & 255] != pc) ring[ri++ & 255] = pc; };
    m.log = [](const std::string& s) {
        std::fprintf(stderr, "[amiga] %s\n", s.c_str());
        if (std::getenv("RF_TRACE") && !dumped && s.find("exception") != std::string::npos) {
            dumped = true;
            for (unsigned k = 0; k < 256; ++k) std::fprintf(stderr, "%06X%c", ring[(ri + k) & 255], (k % 16 == 15) ? '\n' : ' ');
            std::fprintf(stderr, "A7=%08X\n", unsigned(mp->cpu().getA(7)));
        }
    };
    const int frames = std::atoi(argv[2]);
    std::multimap<int, std::string> shots, dumps;
    std::multimap<int, std::pair<int, bool>> keys;
    struct Joy { int start, len, bits, port; };
    std::vector<Joy> joys;
    std::string wav;
    bool wide = false; std::string cdDir; std::multimap<int, std::string> views; uint32_t randomSeed = 0, rnd = 0, rbits[2] = {0, 0};
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--shot" && i + 2 < argc) { shots.insert({std::atoi(argv[i + 1]), argv[i + 2]}); i += 2; }
        else if (a == "--dump" && i + 2 < argc) { dumps.insert({std::atoi(argv[i + 1]), argv[i + 2]}); i += 2; }
        else if (a == "--key" && i + 2 < argc) { const int f = std::atoi(argv[i + 1]); const int k = int(std::strtol(argv[i + 2], nullptr, 16));
            keys.insert({f, {k, true}}); keys.insert({f + 4, {k, false}}); i += 2; }
        else if ((a == "--joy" || a == "--joy1") && i + 3 < argc) {
            joys.push_back({std::atoi(argv[i + 1]), std::atoi(argv[i + 3]), std::atoi(argv[i + 2]), a == "--joy1" ? 0 : 1}); i += 3; }
        else if (a == "--cd" && i + 1 < argc) cdDir = argv[++i];
        else if (a == "--gain" && i + 2 < argc) { m.setGroupGain(0, std::atoi(argv[i + 1])); m.setGroupGain(1, std::atoi(argv[i + 2])); i += 2; }
        else if (a == "--view" && i + 2 < argc) { views.insert({std::atoi(argv[i + 1]), argv[i + 2]}); i += 2; }
        else if (a == "--random" && i + 1 < argc) randomSeed = uint32_t(std::strtoul(argv[++i], nullptr, 0));
        else if (a == "--custom1" && i + 1 < argc) m.setCustom1(uint32_t(std::strtoul(argv[++i], nullptr, 0)));
        else if (a == "--wav" && i + 1 < argc) wav = argv[++i];
    }
    m.setPortDevice(0, amiga::Machine::PortDevice::Joystick);
    if (const char* bp = std::getenv("RF_BP")) {           // RF_BP=hex,hex,...: log every pass (max 20 each)
        std::string l = bp; size_t p = 0;
        static std::map<uint32_t, int> hits;
        while (p < l.size()) {
            size_t q = l.find(',', p); if (q == std::string::npos) q = l.size();
            const uint32_t a = uint32_t(std::strtoul(l.substr(p, q - p).c_str(), nullptr, 16)); p = q + 1;
            m.addHook(a, [a](amiga::Machine& mm) {
                if (hits[a]++ < 20) std::fprintf(stderr, "BP %06X frame %llu d0=%08X a0=%08X\n", a, (unsigned long long)mm.frameCount(), unsigned(mm.cpu().getD(0)), unsigned(mm.cpu().getA(0)));
                mm.cpu().execute();
            });
        }
    }
    if (const char* w = std::getenv("RF_SRWATCH")) {       // RF_SRWATCH=frame: PCs before the IPL mask stays at 7
        static uint64_t from = std::strtoull(w, nullptr, 10); static uint32_t pcs[64]; static unsigned k = 0; static int left = 3; static int run = 0;
        m.onStep = [](amiga::Machine& mm) {
            const uint32_t pc = uint32_t(mm.cpu().getPC()); pcs[k++ & 63] = pc;
            if (mm.frameCount() < from || left <= 0) return;
            if ((mm.cpu().getSR() & 0x700) == 0x700) { if (++run == 2000) { --left; std::fprintf(stderr, "SR=%04X frame %llu, last PCs:", unsigned(mm.cpu().getSR()), (unsigned long long)mm.frameCount());
                for (unsigned i = 0; i < 64; ++i) std::fprintf(stderr, " %06X", pcs[(k + i) & 63]); std::fprintf(stderr, "\n"); } }
            else run = 0;
        };
    }
    if (const char* w = std::getenv("RF_WATCH")) {         // RF_WATCH=hex: log when the long there changes
        static uint32_t wa = uint32_t(std::strtoul(w, nullptr, 16)), last = 0xDEADBEEF; static int n = 0;
        m.onStep = [](amiga::Machine& mm) {
            const uint32_t v = mm.peek32(wa);
            if (v != last && n++ < 40) { std::fprintf(stderr, "WATCH %06X=%08X frame %llu PC=%06X\n", wa, v, (unsigned long long)mm.frameCount(), unsigned(mm.cpu().getPC())); last = v; }
        };
    }
    if (!cdDir.empty()) {
        std::string msg; rf::CdAudio::instance().locate(cdDir, msg); rf::CdAudio::instance().setEnabled(true);
        std::fprintf(stderr, "%s\n", msg.c_str());
    }
    std::string err;
    if (!m.boot("/tmp/rfrun_saves", err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::vector<int16_t> pcm;
    for (int f = 0; f < frames && !m.aborted(); ++f) {
        for (auto it = keys.lower_bound(f); it != keys.upper_bound(f); ++it) m.keyEvent(uint8_t(it->second.first), it->second.second);
        int bits[2] = {0, 0};
        for (auto& j : joys) if (f >= j.start && f < j.start + j.len) bits[j.port] |= j.bits;
        if (randomSeed) {                              // stability test: random joysticks, new state every 6 frames
            if (f == 0) rnd = randomSeed;
            if (f % 6 == 0) for (int p = 0; p < 2; ++p) { rnd = rnd * 1664525u + 1013904223u; rbits[p] = (rnd >> 24) & 31; }
            bits[0] |= int(rbits[0]); bits[1] |= int(rbits[1]);
        }
        for (int p = 0; p < 2; ++p)
            m.setPortJoystick(p, bits[p] & 1, bits[p] & 2, bits[p] & 4, bits[p] & 8, bits[p] & 16);
        m.runFrame();
        if (std::getenv("RF_LOG_INTRO") && f % 100 == 0) std::fprintf(stderr, "intro %d %d\n", f, int(rf::introRunning()));
        if (std::getenv("RF_AUDLOG") && f % 25 == 0) {
            double e = 0; for (int16_t v : m.audioOut()) e += double(v) * v;
            std::fprintf(stderr, "AUD frame %d paula rms %.0f music#%d\n", f, m.audioOut().empty() ? 0.0 : std::sqrt(e / double(m.audioOut().size())), int(m.peek16(0x8CEAC)));
        }
        if (!cdDir.empty()) {
            auto& cd = rf::CdAudio::instance();
            static int lastT = -1; static bool lastP = false;
            cd.update(m);
            auto& a = m.audioOut();
            double pa = 0; for (int16_t s : a) pa += double(s) * s;
            cd.mix(a.data(), a.size() / 2, 44100);
            double pb = 0; for (int16_t s : a) pb += double(s) * s;
            const bool p = pb > pa + 1;
            if (cd.currentTrack() != lastT || p != lastP) {
                std::fprintf(stderr, "CD frame %d track %d %s (paula rms %.0f)\n", f, cd.currentTrack(), p ? "playing" : "silent", a.empty() ? 0.0 : std::sqrt(pa / double(a.size())));
                lastT = cd.currentTrack(); lastP = p;
            }
        }
        if (!wav.empty()) pcm.insert(pcm.end(), m.audioOut().begin(), m.audioOut().end());
        for (auto it = shots.lower_bound(f); it != shots.upper_bound(f); ++it) savePng(m, it->second);
        for (auto it = views.lower_bound(f); it != views.upper_bound(f); ++it) {     // the picture of the window
            const rf::ViewRect vr = rf::viewRect(m);
            const int w = vr.w, h = vr.h;
            std::vector<dm::Rgb> px;
            rf::copyView(m, px, vr);
            std::vector<uint8_t> rgb; for (auto c : px) { rgb.push_back(c.r); rgb.push_back(c.g); rgb.push_back(c.b); }
            dm::writePng(it->second, w, h, rgb);
        }
        if (std::getenv("RF_SPR") && f == std::atoi(std::getenv("RF_SPR"))) std::fprintf(stderr, "%s FMODE=%04X BPLCON3=%04X BPLCON4=%04X\n", m.debugSprites().c_str(), m.customReg(0x1FC), m.customReg(0x106), m.customReg(0x10C));
        if (std::getenv("RF_PAL") && f == std::atoi(std::getenv("RF_PAL"))) {
            for (int i = 0; i < 256; ++i) std::fprintf(stderr, "%06X%c", unsigned(m.colorRgb(i)), i % 16 == 15 ? '\n' : ' ');
            const uint32_t cop = (uint32_t(m.customReg(0x080)) << 16 | m.customReg(0x082));
            std::fprintf(stderr, "COP1LC=%06X\n", unsigned(cop));
            for (int i = 0; i < 160; ++i) std::fprintf(stderr, "%04X %04X%c", m.peek16(cop + 4 * i), m.peek16(cop + 4 * i + 2), i % 8 == 7 ? '\n' : ' ');
        }
        for (auto it = dumps.lower_bound(f); it != dumps.upper_bound(f); ++it) {
            std::ofstream o(it->second, std::ios::binary);
            o.write(reinterpret_cast<const char*>(m.chip()), m.chipSize());
        }
        if (f % 250 == 0) std::fprintf(stderr, "frame %d PC=%06X BPLCON0=%04X FMODE=%04X BPLCON3=%04X BPLCON4=%04X DIW=%04X-%04X DDF=%04X-%04X INTENA=%04X INTREQ=%04X DMACON=%04X SR=%04X\n", f, unsigned(m.cpu().getPC()),
            m.firstBplCon0, m.customReg(0x1FC), m.customReg(0x106), m.customReg(0x10C), m.customReg(0x08E), m.customReg(0x090), m.customReg(0x092), m.customReg(0x094), m.intenaState(), m.intreqState(), m.dmaconState(), unsigned(m.cpu().getSR()));
    }
    if (!wav.empty()) {
        std::ofstream o(wav, std::ios::binary);
        auto u32 = [&](uint32_t v) { o.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&](uint16_t v) { o.write(reinterpret_cast<const char*>(&v), 2); };
        const uint32_t bytes = uint32_t(pcm.size() * 2);
        o.write("RIFF", 4); u32(36 + bytes); o.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32(44100); u32(44100 * 4); u16(4); u16(16);
        o.write("data", 4); u32(bytes); o.write(reinterpret_cast<const char*>(pcm.data()), bytes);
    }
    std::fprintf(stderr, "exceptions:");
    for (int i = 2; i < 64; ++i) if (m.exceptionCount[i]) std::fprintf(stderr, " %d=%u", i, m.exceptionCount[i]);
    std::fprintf(stderr, "\n");
    return 0;
}
