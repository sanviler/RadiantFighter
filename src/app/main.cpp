// RadiantFighter: Shadow Fighter (N.A.P.S. Team / Gremlin, 1994, AGA) on Windows and Linux.
// The original 68000 code runs on a simplified Amiga 1200 (Framework core with the AGA chipset); the game reads the
// user's own disk images (Disk.1-4 of the WHDLoad install or the four ADF files), recognised by their SHA-256 digests.
//
// Keys:
//   F5                        CRT filter on / off
//   Space / Return / fire     during the intro: skip it (straight to the title screen)
//   control ports             set in the options menu: nothing, keyboard, mouse or any joystick for each port
//                             (player 1 plays on port 2, as on the Amiga)
//   keyboard joystick         numeric keypad 8/4/6/2 (7/9/1/3 diagonals), Right Ctrl = fire
//   Escape                    options menu (the game is paused): fullscreen, picture (pixel perfect or fill),
//                             CRT filter, music (CD or Paula),
//                             music and effects volume, move list, ports, quit
//   F11                       window / fullscreen
//   F12                       screenshot (PNG next to the executable)
//   game controller           stick or d-pad = joystick, A = fire, Start = options menu
// Picture: PIXEL PERFECT = largest integer scale (CRT-Pixie filter), FILL = full height at any scale (sharp
//   pixels or the CRT filter for non-integer scales). Both start with the CRT filter on.
// Options: --data <folder>  --fullscreen  --test-frames N (run N frames and quit); for tests --custom1 N (1 skip intro)
#include "amiga/machine.h"
#include "ui/options_panel.h"
#include "rf/cdaudio.h"
#include "rf/disks.h"
#include "rf/moves.h"
#include "rf/options.h"
#include "rf/view.h"
#include "input/input.h"
#include "input/keymap.h"
#include "platform/folder_dialog.h"
#include "platform/png_write.h"
#include "platform/settings.h"
#include "platform/video.h"
#include "version.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
using rf::kViewW; using rf::kViewH;                    // the picture shown in the window (rf/view.h)

std::string toUtf8(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
fs::path toPath(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

void fail(const std::string& msg) {
    std::fprintf(stderr, "%s\n", msg.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RadiantFighter", msg.c_str(), nullptr);
}

std::string portText(const lm::PortLabel& l) {
    switch (l.kind) {
        case lm::DeviceKind::Keyboard: return "KEYBOARD";
        case lm::DeviceKind::Mouse: return "MOUSE";
        case lm::DeviceKind::Joystick: return l.number ? "JOYSTICK " + std::to_string(l.number) : "JOYSTICK (OFF)";
        default: return "NOTHING";
    }
}
// the move list: one fighter per page, shown instead of the options menu; its own picture (w x h), at least
// as large as the options panel
std::vector<dm::Rgb> renderMoves(int page, int& pw, int& ph) {
    const auto& list = rf::moveList();
    const rf::FighterMoves& f = list[size_t(page)];
    int nameW = 0, inW = 0;
    for (const auto& m : f.moves) { nameW = std::max(nameW, dm::OptionsPanel::textWidth(m.name)); inW = std::max(inW, dm::OptionsPanel::textWidth(m.input)); }
    const std::string title = "MOVE LIST", head = "< " + std::string(f.fighter) + " >",
                      count = std::to_string(page + 1) + "/" + std::to_string(list.size()),
                      foot = "LEFT/RIGHT: FIGHTER   ESC: BACK", credit = "FROM THE MOVE LIST BY DRACS (1994)";
    pw = std::max({dm::OptionsPanel::kWidth, nameW + inW + 36, dm::OptionsPanel::textWidth(foot) + 16, dm::OptionsPanel::textWidth(credit) + 16});
    ph = std::max(dm::OptionsPanel::kHeight, 48 + int(f.moves.size()) * 11 + 30);
    const int w = pw, h = ph, x0 = 0, y0 = 0;
    const dm::Rgb border{200, 120, 40}, back{16, 12, 24}, normal{200, 200, 210}, hi{255, 220, 90}, dim{140, 140, 150}, name{130, 150, 255};
    std::vector<dm::Rgb> px(size_t(w) * size_t(h), back);
    dm::OptionsPanel::box(px, w, h, x0, y0, pw, ph, border);
    dm::OptionsPanel::box(px, w, h, x0 + 2, y0 + 2, pw - 4, ph - 4, back);
    dm::OptionsPanel::text(px, w, h, x0 + (pw - dm::OptionsPanel::textWidth(title)) / 2, y0 + 8, title, hi);
    dm::OptionsPanel::text(px, w, h, x0 + pw - 10 - dm::OptionsPanel::textWidth(count), y0 + 8, count, dim);
    dm::OptionsPanel::text(px, w, h, x0 + (pw - dm::OptionsPanel::textWidth(head)) / 2, y0 + 24, head, name);
    for (size_t i = 0; i < f.moves.size(); ++i) {
        const int y = y0 + 42 + int(i) * 11;
        dm::OptionsPanel::text(px, w, h, x0 + 12, y, f.moves[i].name, normal);
        dm::OptionsPanel::text(px, w, h, x0 + pw - 12 - inW, y, f.moves[i].input, hi);
    }
    dm::OptionsPanel::text(px, w, h, x0 + (pw - dm::OptionsPanel::textWidth(credit)) / 2, y0 + ph - 25, credit, dim);
    dm::OptionsPanel::text(px, w, h, x0 + (pw - dm::OptionsPanel::textWidth(foot)) / 2, y0 + ph - 13, foot, dim);
    return px;
}
} // namespace

int main(int argc, char** argv) {
    std::string dataDir;
    bool fullscreen = false;
    uint32_t custom1 = 0;
    long testFrames = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data") dataDir = next();
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--custom1") { custom1 = uint32_t(std::strtoul(next().c_str(), nullptr, 0)); }
        else if (a == "--test-frames") testFrames = std::atol(next().c_str());
    }
    const bool testing = testFrames >= 0;

    dm::Video::hints();                              // before SDL_Init: real pixels with Windows display scaling
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) std::fprintf(stderr, "no audio: %s\n", SDL_GetError());
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) std::fprintf(stderr, "no game controllers: %s\n", SDL_GetError());

    fs::path exeDir = fs::current_path();
    if (char* b = SDL_GetBasePath()) { exeDir = toPath(b); SDL_free(b); }
    const std::string settingsPath = toUtf8(exeDir / "radiantfighter.ini");
    dm::Settings settings;
    const bool haveSettings = !testing && settings.load(settingsPath);

    // the user's disks: --data, game_path in radiantfighter.ini (default: next to the executable), the current folder
    auto resolve = [&](const std::string& p) {
        const fs::path q = toPath(p.empty() ? std::string(".") : p);
        return toUtf8((q.is_absolute() ? q : exeDir / q).lexically_normal());
    };
    auto& disks = rf::Disks::instance();
    const std::string gamePath = !dataDir.empty() ? dataDir : settings.get("game_path", ".");
    std::string msg;
    bool found = disks.locate({resolve(gamePath), toUtf8(fs::current_path())}, msg);
    while (!found) {
        if (testing || !dm::folderDialogAvailable()) { fail(msg); SDL_Quit(); return 1; }
        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose folder..."},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"}};
        const std::string text = msg + "\n\nChoose the folder that contains Disk.1 ... Disk.4?";
        const SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT, nullptr, "RadiantFighter",
                                     text.c_str(), 2, buttons, nullptr};
        int choice = 0;
        if (SDL_ShowMessageBox(&box, &choice) != 0 || choice != 1) { SDL_Quit(); return 1; }
        std::string chosen;
        if (!dm::chooseFolder("Choose the folder with the Shadow Fighter disks (Disk.1 ... Disk.4)", toUtf8(exeDir), chosen)) continue;
        msg.clear();
        found = disks.locate({chosen}, msg);
        if (found) {
            std::error_code ec;
            const fs::path rel = fs::relative(toPath(chosen), exeDir, ec);
            const bool inside = !ec && !rel.empty() && rel.begin()->string() != "..";
            settings.set("game_path", inside ? toUtf8(rel) : chosen);
            settings.save(settingsPath);
        }
    }
    std::fprintf(stderr, "%s: %s\n", msg.c_str(), disks.folder().c_str());

    // settings
    bool crtOn = settings.get("crt", "on") == "on";
    bool fillMode = settings.get("picture", "pixel") == "fill";
    // CD audio: WAV files track02.wav ... track13.wav in the "audio" folder next to the executable (created here)
    auto& cd = rf::CdAudio::instance();
    {
        std::string cdMsg;
        cd.locate(toUtf8(exeDir / "audio"), cdMsg);
        std::fprintf(stderr, "%s\n", cdMsg.c_str());
    }
    // music: "cd" (the CD32 tracks) or "paula" (the original Amiga music); CD without tracks = Paula
    bool cdMusic = settings.get("music", settings.get("cd_audio", "on") == "on" ? "cd" : "paula") == "cd";
    cd.setEnabled(cdMusic);
    auto volSetting = [&](const char* key, int def) { const int v = std::atoi(settings.get(key, std::to_string(def)).c_str()); return std::clamp(v / 10 * 10, 0, 100); };
    int musicVol = volSetting("music_volume", 80), effectsVol = volSetting("effects_volume", 100);
    if (!fullscreen) fullscreen = settings.get("fullscreen", "off") == "on";

    lm::InputPorts ports;
    if (haveSettings && !settings.get("port2").empty()) ports.load(settings.get("port1"), settings.get("port2"));
    else {
        ports.setDefaults();
        if (ports.label(0).kind == lm::DeviceKind::Mouse) ports.load("none", ports.save(1));   // no mouse in Shadow Fighter
    }

    dm::Video video;
    const std::string title = std::string("RadiantFighter ver. ") + RF_VERSION_STRING + " (Shadow Fighter)";
    if (!video.init(title.c_str(), kViewW, kViewH, 3)) { fail(std::string("video: ") + SDL_GetError()); SDL_Quit(); return 1; }
    bool isFull = false;
    if (fullscreen) { video.toggleFullscreen(); isFull = true; }
    video.setCrt(crtOn);
    video.setMode(fillMode ? dm::Video::Mode::Fill : dm::Video::Mode::PixelPerfect);
    auto saveSettings = [&] {
        if (testing) return;
        settings.set("crt", crtOn ? "on" : "off");
        settings.set("picture", fillMode ? "fill" : "pixel");
        settings.set("music", cdMusic ? "cd" : "paula");
        settings.set("music_volume", std::to_string(musicVol));
        settings.set("effects_volume", std::to_string(effectsVol));
        settings.set("fullscreen", isFull ? "on" : "off");
        settings.set("port1", ports.save(0));
        settings.set("port2", ports.save(1));
        if (settings.get("game_path").empty()) settings.set("game_path", ".");
        settings.save(settingsPath);
    };

    std::unique_ptr<amiga::Machine> mp;
    auto applyVolumes = [&] {
        cd.setGain(musicVol * 256 / 100);
        if (mp) { mp->setGroupGain(1, musicVol * 256 / 100); mp->setGroupGain(0, effectsVol * 256 / 100); }
    };
    bool running = true, shotRequested = false, resetRequested = false, skipNow = false;
    std::string err;
    auto start = [&](uint32_t c1) {
        mp = std::make_unique<amiga::Machine>();
        mp->log = [](const std::string& s) { std::fprintf(stderr, "[amiga] %s\n", s.c_str()); };
        mp->setCustom1(c1);
        applyVolumes();
        return mp->boot(toUtf8(exeDir), err);
    };
    if (!start(custom1)) { fail(err); SDL_Quit(); return 1; }

    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    const SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio) { mp->setAudioRate(have.freq); SDL_PauseAudioDevice(audio, 0); }
    const uint32_t bytesPerFrame = uint32_t(have.freq ? have.freq : 44100) / 50 * 4;

    dm::OptionsPanel menu;
    enum { ItFull, ItPicture, ItCrt, ItMusic, ItMusicVol, ItFxVol, ItMoves, ItPort1, ItPort2, ItReset, ItQuit, ItBack, ItCount };
    bool movesOpen = false;          // the move list (a page per fighter) over the menu
    int movesPage = 0;
    const int movesCount = int(rf::moveList().size());
    bool amigaKeyDown[0x80] = {};
    auto releaseKeys = [&] {
        for (int k = 0; k < 0x80; ++k) if (amigaKeyDown[k]) { amigaKeyDown[k] = false; mp->keyEvent(uint8_t(k), false); }
    };
    auto activate = [&](int item, int dir) {        // dir 0 = Enter, -1 / +1 = left / right
        switch (item) {
            case ItFull: video.toggleFullscreen(); isFull = !isFull; break;
            case ItPicture: fillMode = !fillMode; crtOn = true; video.setCrt(true);      // each mode starts with its CRT filter
                video.setMode(fillMode ? dm::Video::Mode::Fill : dm::Video::Mode::PixelPerfect); break;
            case ItCrt: crtOn = !crtOn; video.setCrt(crtOn); break;
            case ItMusic: if (cd.available()) { cdMusic = !cdMusic; cd.setEnabled(cdMusic); } break;
            case ItMusicVol: musicVol = std::clamp(musicVol + (dir ? dir : 1) * 10, 0, 100); applyVolumes(); break;
            case ItFxVol: effectsVol = std::clamp(effectsVol + (dir ? dir : 1) * 10, 0, 100); applyVolumes(); break;
            case ItMoves: if (!dir) { movesOpen = true; } break;
            case ItPort1: ports.cycle(0, dir ? dir : 1); break;
            case ItPort2: ports.cycle(1, dir ? dir : 1); break;
            case ItReset: if (!dir) { resetRequested = true; menu.open = false; } break;
            case ItQuit: if (!dir) running = false; break;
            case ItBack: if (!dir) menu.open = false; break;
        }
        saveSettings();
    };

    uint64_t nextTick = SDL_GetPerformanceCounter();
    const uint64_t perFrame = SDL_GetPerformanceFrequency() / 50;
    int shotN = 0;
    uint64_t fpsStart = SDL_GetTicks64();
    int fpsFrames = 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ports.handleEvent(ev);
            switch (ev.type) {
                case SDL_QUIT: running = false; break;
                case SDL_CONTROLLERBUTTONDOWN: {
                    const int b = ev.cbutton.button;
                    if (b == SDL_CONTROLLER_BUTTON_START) { menu.open = !menu.open; menu.focus = 0; movesOpen = false; releaseKeys(); }
                    else if (!menu.open && rf::introRunning() && b == SDL_CONTROLLER_BUTTON_A) skipNow = true;
                    else if (menu.open && movesOpen) {
                        if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT || b == SDL_CONTROLLER_BUTTON_DPAD_UP) movesPage = (movesPage + movesCount - 1) % movesCount;
                        else if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT || b == SDL_CONTROLLER_BUTTON_DPAD_DOWN || b == SDL_CONTROLLER_BUTTON_A) movesPage = (movesPage + 1) % movesCount;
                        else if (b == SDL_CONTROLLER_BUTTON_B) movesOpen = false;
                    }
                    else if (menu.open) {
                        if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) menu.move(-1, ItCount);
                        else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) menu.move(1, ItCount);
                        else if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT) activate(menu.focus, -1);
                        else if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) activate(menu.focus, 1);
                        else if (b == SDL_CONTROLLER_BUTTON_A) activate(menu.focus, 0);
                        else if (b == SDL_CONTROLLER_BUTTON_B) menu.open = false;
                    }
                    break;
                }
                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    const SDL_Scancode sc = ev.key.keysym.scancode;
                    const bool dn = ev.type == SDL_KEYDOWN;
                    if (dn && sc == SDL_SCANCODE_F11) { activate(ItFull, 1); break; }
                    if (dn && sc == SDL_SCANCODE_F12) { shotRequested = true; break; }
                    if (dn && sc == SDL_SCANCODE_F5) { activate(ItCrt, 1); break; }
                    if (menu.open && movesOpen) {
                        if (!dn) break;
                        if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_BACKSPACE) movesOpen = false;
                        else if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_UP) movesPage = (movesPage + movesCount - 1) % movesCount;
                        else if (sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_SPACE) movesPage = (movesPage + 1) % movesCount;
                        break;
                    }
                    if (menu.open) {
                        if (!dn) break;
                        if (sc == SDL_SCANCODE_ESCAPE) menu.open = false;
                        else if (sc == SDL_SCANCODE_UP) menu.move(-1, ItCount);
                        else if (sc == SDL_SCANCODE_DOWN) menu.move(1, ItCount);
                        else if (sc == SDL_SCANCODE_LEFT) activate(menu.focus, -1);
                        else if (sc == SDL_SCANCODE_RIGHT) activate(menu.focus, 1);
                        else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_SPACE) activate(menu.focus, 0);
                        break;
                    }
                    if (dn && sc == SDL_SCANCODE_ESCAPE) { menu.open = true; menu.focus = 0; releaseKeys(); break; }
                    if (dn && rf::introRunning() && (sc == SDL_SCANCODE_SPACE || sc == SDL_SCANCODE_RETURN ||
                        sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_RCTRL)) { skipNow = true; break; }
                    if (ports.isJoystickKey(sc) || ev.key.repeat) break;
                    const auto& km = lm::amigaKeymap();
                    if (auto it = km.find(sc); it != km.end() && amigaKeyDown[it->second] != dn) {
                        amigaKeyDown[it->second] = dn;
                        mp->keyEvent(it->second, dn);
                    }
                    break;
                }
                default: break;
            }
        }
        if (skipNow) {                               // the intro is skipped: the game starts again at the title screen
            skipNow = false;
            releaseKeys();
            cd.stop();
            if (!start(custom1 | rf::kSkipIntro)) { fail(err); break; }
            if (audio) { mp->setAudioRate(have.freq); SDL_ClearQueuedAudio(audio); }
        }
        if (resetRequested) {
            resetRequested = false;
            releaseKeys();
            cd.stop();
            if (!start(custom1)) { fail(err); break; }
            if (audio) { mp->setAudioRate(have.freq); SDL_ClearQueuedAudio(audio); }
        }
        amiga::Machine& m = *mp;
        ports.apply(m, 0, menu.open);

        // 50 Hz: with audio the queue of samples sets the pace, otherwise the timer
        const bool runNow = testing || (audio ? SDL_GetQueuedAudioSize(audio) < bytesPerFrame * 3 : SDL_GetPerformanceCounter() >= nextTick);
        if (!runNow) { SDL_Delay(1); continue; }
        nextTick += perFrame;
        if (menu.open) {
            if (audio && SDL_GetQueuedAudioSize(audio) < bytesPerFrame * 3) {
                std::vector<int16_t> silence(bytesPerFrame / 2, 0);
                SDL_QueueAudio(audio, silence.data(), bytesPerFrame);
            }
        } else {
            m.runFrame();
            cd.update(m);
            if (audio) {
                auto& a = m.audioOut();
                cd.mix(a.data(), a.size() / 2, have.freq);
                if (!a.empty()) SDL_QueueAudio(audio, a.data(), uint32_t(a.size() * sizeof(int16_t)));
            }
            const rf::ViewRect vr = rf::viewRect(m);
            if (vr.w != video.width() || vr.h != video.height()) video.resizeLogical(vr.w, vr.h);
            rf::copyView(m, video.frame(), vr);
        }
        if (shotRequested) {                         // the picture without the menu, at 3x
            shotRequested = false;
            const int w = video.width(), h = video.height(), s = 3;
            std::vector<uint8_t> rgb(size_t(w) * h * s * s * 3);
            for (int y = 0; y < h * s; ++y)
                for (int x = 0; x < w * s; ++x) {
                    const dm::Rgb c = video.frame()[size_t(y / s) * w + x / s];
                    uint8_t* p = &rgb[(size_t(y) * w * s + x) * 3];
                    p[0] = c.r; p[1] = c.g; p[2] = c.b;
                }
            fs::path p;
            do { p = exeDir / ("radiantfighter_" + std::to_string(++shotN) + ".png"); } while (fs::exists(p));
            dm::writePng(toUtf8(p), w * s, h * s, rgb);
        }
        if (menu.open) {
            auto bar = [](int v) { return std::string(size_t(v / 10), '#') + std::string(size_t(10 - v / 10), '.') + " " + std::to_string(v) + "%"; };
            const std::vector<dm::MenuItem> items = {
                {"SCREEN", isFull ? "FULL" : "WINDOW"},
                {"PICTURE", fillMode ? "FILL" : "PIXEL PERFECT"},
                {"CRT FILTER (F5)", crtOn ? "ON" : "OFF"},
                {"MUSIC", !cd.available() ? "PAULA (NO CD TRACKS)" : (cdMusic ? "CD" : "PAULA")},
                {"MUSIC VOLUME", bar(musicVol)},
                {"EFFECTS VOLUME", bar(effectsVol)},
                {"MOVE LIST", ""},
                {"PORT 1 (PLAYER 2)", portText(ports.label(0))},
                {"PORT 2 (PLAYER 1)", portText(ports.label(1))},
                {"RESTART GAME", ""}, {"QUIT", ""}, {"BACK", ""}};
            menu.footer.clear();
            if (menu.focus == ItPort1 || menu.focus == ItPort2) menu.footer = ports.label(menu.focus - ItPort1).name;
            else if (menu.focus == ItMusic) menu.footer = cd.available() ? "CD: CD32 TRACKS   PAULA: ORIGINAL AMIGA MUSIC" : "CD TRACKS: AUDIO FOLDER, TRACK02.WAV ... TRACK13.WAV";
            else if (menu.focus == ItFxVol) menu.footer = "SOUND EFFECTS AND STAGE SOUNDS";
            for (auto& ch : menu.footer) ch = char(std::toupper(static_cast<unsigned char>(ch)));
            // the panels are drawn over the filtered picture (never through the CRT filter), at their own scale
            const auto keep = video.frame();
            dm::OptionsPanel::darken(video.frame());
            int ow = 0, oh = 0;
            video.outputSize(ow, oh);
            if (movesOpen) {
                int mw = 0, mh = 0;
                const std::vector<dm::Rgb> mp = renderMoves(movesPage, mw, mh);
                video.setOverlay(mp, mw, mh, dm::OptionsPanel::scaleFor(ow, oh, mw, mh));
            } else {
                menu.render(items);
                video.setOverlay(menu.pixels(), menu.width(), menu.height(), dm::OptionsPanel::scaleFor(ow, oh, menu.width(), menu.height()));
            }
            video.present();
            video.clearOverlay();
            video.frame() = keep;
        } else video.present();
        ++fpsFrames;                                 // window title: name, version and the frames shown per second
        if (const uint64_t now = SDL_GetTicks64(); now - fpsStart >= 1000) {
            const int fps = int((fpsFrames * 1000 + (now - fpsStart) / 2) / (now - fpsStart));
            video.setTitle(title + " - " + std::to_string(fps) + " fps");
            fpsStart = now;
            fpsFrames = 0;
        }
        if (testing && long(m.frameCount()) >= testFrames) running = false;
    }
    saveSettings();
    if (audio) SDL_CloseAudioDevice(audio);
    video.shutdown();
    SDL_Quit();
    return 0;
}
