// amirun: the Amiga machine without a window, for tests and comparisons (developer tool, not shipped).
// Usage: amirun <game folder> <frames> [--shot N file.ppm]... [--key N code] [--joy N bits length] [--wav file]
//        amirun <game folder> --stdin        command mode, one command per line, answer "ok":
//          frames N | peek ADDR LEN | poke ADDR HEX | joy BITS | key HEX down|up | shot FILE | frame |
//          live FILE EXTRA | textmask FILE | panelmask FILE | quit
// The game folder holds the four ADF images or the WHDLoad "data" folder (see formats/game_files.h).
// Environment: DM_ROUTINES=off|replace|verify, DM_HAMFIX=off, DM_PANEL_LOG=1, DM_CUSTOM1=N (cheats).
#include "amiga/machine.h"
#include "amiga/cpu.h"
#include "formats/game_files.h"
#include "game/fixes.h"
#include "game/ham_fix.h"
#include "game/live_view.h"
#include "game/panel_text.h"
#include "game/routines.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

static void savePpm(const amiga::Machine& m, const std::string& path) {
    std::ofstream o(path, std::ios::binary);
    o << "P6 " << amiga::Machine::kFbWidth << " " << amiga::Machine::kFbHeight << " 255\n";
    for (uint32_t c : m.framebuffer()) { o.put(char(c >> 16)); o.put(char(c >> 8)); o.put(char(c)); }
}

static void savePgm(const dm::IndexedFrame& f, const std::string& path) {
    std::ofstream o(path, std::ios::binary);
    o << "P5 " << f.width << " " << f.height << " 255\n";
    o.write(reinterpret_cast<const char*>(f.px.data()), std::streamsize(f.px.size()));
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: amirun <game folder> <frames|--stdin> [options]\n"); return 1; }
    // older scripts passed a WHDLoad slave as second argument: it is not needed any more
    int ai = 2;
    if (argc > 3 && std::strcmp(argv[2], "--stdin") != 0 && !(argv[2][0] >= '0' && argv[2][0] <= '9')) ai = 3;
    std::string msg;
    if (!dm::GameFiles::instance().locate({argv[1]}, msg)) { std::fprintf(stderr, "%s\n", msg.c_str()); return 1; }
    amiga::Machine m;
    m.log = [](const std::string& s) { std::fprintf(stderr, "[amiga] %s\n", s.c_str()); };
    static dm::game::Routines routineStats;
    dm::game::HookMode rmode = dm::game::HookMode::Replace;
    if (const char* r = std::getenv("DM_ROUTINES")) rmode = std::string(r) == "off" ? dm::game::HookMode::Off
        : std::string(r) == "verify" ? dm::game::HookMode::Verify : dm::game::HookMode::Replace;
    static dm::game::HamFix hamFix;
    if (!std::getenv("DM_HAMFIX") || std::string(std::getenv("DM_HAMFIX")) != "off") hamFix.load();
    static dm::game::PanelText panelText;
    panelText.log = std::getenv("DM_PANEL_LOG") != nullptr;
    m.onGamePatched = [rmode](amiga::Machine& mm) { dm::game::applyFixes(mm); dm::game::installRoutines(mm, rmode, routineStats); panelText.install(mm); };
    std::atexit([] { std::fprintf(stderr, "%s\n", dm::game::describe(routineStats).c_str()); });
    if (const char* c1 = std::getenv("DM_CUSTOM1")) m.setCustom1(uint32_t(std::strtoul(c1, nullptr, 0)));
    std::string err;
    if (!m.boot("/tmp/amirun_saves", err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    if (std::string(argv[ai]) == "--stdin") {
        bool up = false, dn = false, lf = false, rt = false, fi = false;
        char line[512];
        std::printf("ready\n"); std::fflush(stdout);
        static dm::game::LiveView* lv0 = nullptr;
        auto liveView = [&]() -> dm::game::LiveView& {
            if (!lv0) lv0 = new dm::game::LiveView(dm::game::LiveView::loadMainCode(), 0);
            return *lv0;
        };
        while (std::fgets(line, sizeof line, stdin)) {
            char cmd[32] = {0}, a1[400] = {0}, a2[400] = {0};
            std::sscanf(line, "%31s %399s %399s", cmd, a1, a2);
            const std::string c = cmd;
            if (c == "quit") break;
            if (c == "frames") { const long n = std::atol(a1); for (long i = 0; i < n && !m.aborted(); ++i) { m.setJoystick(up, dn, lf, rt, fi); m.runFrame(); hamFix.apply(m); } }
            else if (c == "frame") std::printf("%llu\n", static_cast<unsigned long long>(m.frameCount()));
            else if (c == "peek") { const uint32_t a = uint32_t(std::strtoul(a1, nullptr, 0)); const long n = std::atol(a2);
                for (long i = 0; i < n; ++i) { std::printf("%02x", m.peek8(a + uint32_t(i))); } std::printf("\n"); }
            else if (c == "poke") { const uint32_t a = uint32_t(std::strtoul(a1, nullptr, 0)); const std::string h = a2;
                for (size_t i = 0; i + 1 < h.size(); i += 2) m.poke8(a + uint32_t(i / 2), uint8_t(std::stoi(h.substr(i, 2), nullptr, 16))); }
            else if (c == "joy") { const int b = std::atoi(a1); up = b & 1; dn = b & 2; lf = b & 4; rt = b & 8; fi = b & 16; }
            else if (c == "key") m.keyEvent(uint8_t(std::strtoul(a1, nullptr, 16)), std::string(a2) == "down");
            else if (c == "shot") savePpm(m, a1);
            else if (c == "live") {                                   // the C++ live view, PPM
                static dm::game::LiveView lvw(dm::game::LiveView::loadMainCode(), std::atoi(a2));
                dm::IndexedFrame f;
                if (lvw.render(m, f, int(m.frameCount() / 6))) {
                    std::ofstream o(a1, std::ios::binary);
                    o << "P6 " << f.width << " " << f.height << " 255\n";
                    for (uint8_t v : f.px) { const auto c2 = f.palette.c[v]; o.put(char(c2.r)); o.put(char(c2.g)); o.put(char(c2.b)); }
                } else std::printf("no room\n");
            }
            else if (c == "textmask") {                               // panel strings rewritten in C++ (PGM, indices)
                dm::IndexedFrame f; f.width = 320; f.height = 200; f.px.assign(size_t(320) * 200, 0);
                liveView().renderText(m, panelText.roomName, 24, 183, f);
                liveView().renderText(m, panelText.targetName, 192, 183, f);
                savePgm(f, a1);
                std::printf("%s|%s\n", panelText.roomName.c_str(), panelText.targetName.c_str());
            }
            else if (c == "panelmask") {                              // panel rebuilt from the recorded draws (PGM)
                dm::IndexedFrame f; f.width = 320; f.height = 200; f.px.assign(size_t(320) * 200, 0);
                for (const auto& d : panelText.panelDraws()) liveView().renderDescriptor(m, d.x, d.y, d.descriptor, f);
                savePgm(f, a1);
                std::printf("%zu\n", panelText.panelDraws().size());
                for (const auto& d : panelText.panelDraws()) std::printf("%d,%d,%d ", d.x, d.y, d.descriptor);
                std::printf("\n");
            }
            std::printf("ok\n"); std::fflush(stdout);
        }
        return 0;
    }
    const int frames = std::atoi(argv[ai]);
    std::multimap<int, std::string> shots;
    std::multimap<int, std::pair<int, bool>> keys;
    struct Joy { int start, len, bits; };
    std::vector<Joy> joys;
    std::string wav;
    std::vector<int16_t> pcm;
    for (int i = ai + 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--shot" && i + 2 < argc) { shots.insert({std::atoi(argv[i + 1]), argv[i + 2]}); i += 2; }
        else if (a == "--key" && i + 2 < argc) { int f = std::atoi(argv[i + 1]), k = int(std::strtol(argv[i + 2], nullptr, 16)); keys.insert({f, {k, true}}); keys.insert({f + 3, {k, false}}); i += 2; }
        else if (a == "--wav" && i + 1 < argc) { wav = argv[++i]; }
        else if (a == "--joy" && i + 3 < argc) { joys.push_back({std::atoi(argv[i + 1]), std::atoi(argv[i + 3]), std::atoi(argv[i + 2])}); i += 3; }
    }
    for (int f = 0; f < frames && !m.aborted(); ++f) {
        for (auto it = keys.lower_bound(f); it != keys.upper_bound(f); ++it) m.keyEvent(uint8_t(it->second.first), it->second.second);
        int bits = 0;
        for (auto& j : joys) if (f >= j.start && f < j.start + j.len) bits |= j.bits;
        m.setJoystick(bits & 1, bits & 2, bits & 4, bits & 8, bits & 16, bits & 32);
        m.runFrame();
        hamFix.apply(m);
        if (!wav.empty()) pcm.insert(pcm.end(), m.audioOut().begin(), m.audioOut().end());
        for (auto it = shots.lower_bound(f); it != shots.upper_bound(f); ++it) savePpm(m, it->second);
        if (f % 50 == 0) std::fprintf(stderr, "frame %d PC=%06X\n", f, unsigned(m.cpu().getPC()));
    }
    if (!wav.empty()) {
        std::ofstream o(wav, std::ios::binary);
        auto u32 = [&](uint32_t v) { o.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&](uint16_t v) { o.write(reinterpret_cast<const char*>(&v), 2); };
        const uint32_t bytes = uint32_t(pcm.size() * 2);
        o.write("RIFF", 4); u32(36 + bytes); o.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32(44100); u32(44100 * 4); u16(4); u16(16);
        o.write("data", 4); u32(bytes); o.write(reinterpret_cast<const char*>(pcm.data()), bytes);
    }
    return 0;
}
