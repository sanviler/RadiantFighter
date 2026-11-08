#pragma once
#include <algorithm>
// A simplified Amiga 500 that runs the original code of Darkmere.
//
// It contains only what the game uses:
//  - 68000 CPU (Moira, MIT licence, in ext/moira)
//  - 512 KB of chip RAM, expansion memory for the game, a private area for the loader hooks
//  - optional AGA mode (setAga): 2 MB of chip RAM, 8 bitplanes, 256 colours of 24 bits (BPLCON3 bank/LOCT),
//    BPLCON4 (bitplane XOR, sprite banks), FMODE bitplane fetch width, HAM8, KILLEHB, Lisa/Alice IDs
//  - custom chips: interrupts, copper, blitter, bitplanes, sprites, audio (Paula), joystick and mouse
//  - two 8520 CIAs (timers, TOD, keyboard, buttons)
//  - the game loader (loader.cpp) reading the original files: no Kickstart, no disk drives.
//
// Time advances by PAL lines (313 lines of 227 colour clocks; 2 CPU cycles per colour clock).
// Every line is split into steps of 4 colour clocks: copper, CPU, bitplane DMA; the line is
// drawn at its end with the colour changes recorded during the line.
//
// Hooks: any address of the 68000 code can be replaced by a C++ function.
// This is how the game routines are translated to C++, one at a time.
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace amiga {

class Machine;

// --------------------------------------------------------------------------------------------
// CIA 8520
// --------------------------------------------------------------------------------------------
struct Cia {
    uint8_t pra = 0xFF, prb = 0xFF, ddra = 0, ddrb = 0;
    uint16_t ta = 0xFFFF, tb = 0xFFFF, taLatch = 0xFFFF, tbLatch = 0xFFFF;
    uint8_t cra = 0, crb = 0;
    uint8_t icrMask = 0, icrData = 0;
    uint8_t sdr = 0;
    uint32_t tod = 0, todAlarm = 0, todLatch = 0;
    bool todLatched = false, todStopped = false;
    double eAccum = 0;          // E cycles (CPU/10) still to run
    uint8_t (*inputA)(Machine&) = nullptr;   // reads the input pins of port A

    // returns true if the interrupt must be requested
    bool tickE(int n);
    void tickTod();
    void setIcr(uint8_t bits) { icrData |= bits; }
    bool irqPending() const { return (icrData & icrMask & 0x1F) != 0; }
};

// --------------------------------------------------------------------------------------------
// CPU
// --------------------------------------------------------------------------------------------
class Cpu;

// --------------------------------------------------------------------------------------------
// Machine
// --------------------------------------------------------------------------------------------
class Machine {
public:
    static constexpr uint32_t kChipSize = 0x80000;          // OCS/ECS (default)
    static constexpr uint32_t kChipSizeAga = 0x200000;      // AGA mode
    static constexpr uint32_t kExpBase = 0x200000;          // expansion memory
    static constexpr uint32_t kPrivateBase = 0x400000;      // port-private memory (hook entry points)
    static constexpr uint32_t kPrivateSize = 0x10000;
    static constexpr int kLines = 313, kCck = 227;
    static constexpr int kCpuCyclesPerLine = kCck * 2;
    // frame buffer: hires resolution (2 pixels per lores pixel), lines 26..312
    static constexpr int kFbFirstLine = 26, kFbHeight = kLines - kFbFirstLine;
    // lores $50-$1E0: the whole width where the hardware can fetch bitplanes (wide window, see setWideWindow)
    static constexpr int kFbFirstLores = 0x50, kFbLoresWidth = 400, kFbWidth = kFbLoresWidth * 2;

    Machine();
    ~Machine();

    // --- start ---
    // the game files must be mounted first (dm::GameFiles); saveDir: where the save disk image is written
    bool boot(const std::string& saveDir, std::string& err);
    void setCustom1(uint32_t v) { custom1_ = v; }
    // AGA chipset (before boot): 2 MB chip RAM, AGA video registers. Off by default (OCS behaviour unchanged).
    void setAga(bool on);
    bool aga() const { return aga_; }
    // Wide window (port feature, off by default): the display window is opened to every pixel the bitplane DMA
    // fetched on the line, so the picture shows the graphics that the original window hides at the sides.
    // planeMask: the planes drawn in full outside the original window; the others are drawn outside it only in
    // the first limitPx pixels of the fetch (0 = not at all), for planes whose rows are shorter than the fetch.
    void setWideWindow(bool on, uint8_t planeMask = 0xFF, int limitPx = 0) { wide_ = on; wideMask_ = planeMask; wideLimit_ = limitPx; }
    bool wideWindow() const { return wide_; }
    uint32_t chipSize() const { return chipMask_ + 1; }

    // --- running ---
    void runFrame();
    bool aborted() const { return aborted_; }
    uint64_t frameCount() const { return frames_; }

    // --- video: RGB 0x00RRGGBB, kFbWidth x kFbHeight ---
    const std::vector<uint32_t>& framebuffer() const { return fb_; }

    // --- audio: 16-bit stereo samples (L, R) produced by the last call ---
    void setAudioRate(int hz) { audioRate_ = hz; }
    std::vector<int16_t>& audioOut() { return audioOut_; }
    // Audio groups (optional, all 256 by default): each channel takes the group set with setAudioGroup when its
    // period or volume is written (e.g. 1 while the game's music replayer runs, 0 otherwise); setGroupGain sets the
    // volume of a group (0-256), so music and sound effects played on Paula can have separate volumes.
    void setAudioGroup(int g) { audioGroup_ = g & 3; }
    int audioGroup() const { return audioGroup_; }
    void setGroupGain(int g, int gain) { groupGain_[size_t(g & 3)] = std::clamp(gain, 0, 512); }

    // --- input ---
    void keyEvent(uint8_t rawCode, bool down);                 // raw Amiga keyboard codes
    // The two control ports. Port 1 (index 0) is a mouse and port 2 (index 1) a joystick, as on a standard
    // Amiga; either can be set to a mouse or a joystick. Joystick: directions in JOYxDAT, fire on CIA-A PRA
    // (bit 6 port 1, bit 7 port 2), second button on pin 9 (POTINP). Mouse: counters in JOYxDAT, left button
    // as fire, right button on pin 9.
    enum class PortDevice { Joystick, Mouse };
    void setPortDevice(int port, PortDevice d);
    void setPortJoystick(int port, bool up, bool down, bool left, bool right, bool fire, bool fire2 = false);
    void setPortMouse(int port, int dx, int dy, bool left, bool right);   // relative motion and buttons
    // port 2 joystick / port 1 mouse (shortcuts)
    void setJoystick(bool up, bool down, bool left, bool right, bool fire, bool fire2 = false);
    void mouseMove(int dx, int dy);
    void setMouseButtons(bool left, bool right);

    // --- memory (for tools and hooks) ---
    uint8_t peek8(uint32_t a) const;
    uint16_t peek16(uint32_t a) const;
    uint32_t peek32(uint32_t a) const;
    void poke8(uint32_t a, uint8_t v);
    void poke16(uint32_t a, uint16_t v);
    void poke32(uint32_t a, uint32_t v);
    uint8_t* chip() { return chip_.data(); }
    uint32_t gameBase() const { return gameBase_; }           // address of the first hunk of MAIN.BIN

    // --- hooks ---
    using Hook = std::function<void(Machine&)>;
    void addHook(uint32_t addr, Hook h);
    Cpu& cpu() { return *cpu_; }
    void returnFromSubroutine();                               // emulated rts (for hooks)
    void jumpTo(uint32_t addr);

    // diagnostics
    std::function<void(const std::string&)> log;
    // called once MAIN.BIN is loaded and patched: Lightmere applies its own fixes here
    std::function<void(Machine&)> onGamePatched;
    // diagnostics: called at every step of 4 colour clocks (PC profile); empty = no cost
    std::function<void(Machine&)> onStep;
    std::array<uint32_t, 64> exceptionCount{};
    std::function<void(Machine&)> onBlit;                // diagnostics: called before every blit
    uint32_t firstBpl1pt = 0;
    uint32_t firstBplPt[8] = {};                 // pointers of the planes (6 OCS, 8 AGA) at the first visible line of the frame
    uint16_t firstBplCon0 = 0;                   // BPLCON0 at the first visible line
    bool noSprites_ = false;
    std::string debugSprites() const;                             // diagnostics: state of the sprites
    uint16_t customReg(uint32_t off) const { return reg_[(off & 0x1FE) / 2]; }          // diagnostics
    uint16_t intenaState() const { return intena_; }                                   // diagnostics
    uint16_t intreqState() const { return intreq_; }                                   // diagnostics
    uint16_t dmaconState() const { return dmacon_; }                                   // diagnostics
    uint32_t colorRgb(int i) const { return color_[size_t(i & 255)]; }                 // diagnostics: 0x00RRGGBB

    // --- access from the CPU core (not for outside use) ---
    uint8_t busRead8(uint32_t a);
    uint16_t busRead16(uint32_t a);
    void busWrite8(uint32_t a, uint8_t v);
    void busWrite16(uint32_t a, uint16_t v);

private:
    friend class Cpu;
    // memory
    std::vector<uint8_t> chip_, exp_, private_;
    uint32_t expSize_ = 0;
    uint8_t* mem(uint32_t a, uint32_t n);                     // pointer if RAM, otherwise nullptr
    const uint8_t* mem(uint32_t a, uint32_t n) const;

    std::unique_ptr<Cpu> cpu_;
    std::unordered_map<uint32_t, Hook> hooks_;
    std::vector<uint8_t> hookMap_;                            // 1 bit per word: hooked address

    // time
    int64_t lineStart_ = 0;
    int vpos_ = 0;
    int hposNow() const;
    uint64_t frames_ = 0;
    bool aborted_ = false;

    // --- chip custom ---
    std::array<uint16_t, 0x100> reg_{};                       // last value written (index = offset/2)
    uint16_t dmacon_ = 0, intena_ = 0, intreq_ = 0, adkcon_ = 0;
    void customWrite(uint32_t off, uint16_t v);
    uint16_t customRead(uint32_t off);
    void updateIpl();
    void raise(int bit) { intreq_ |= uint16_t(1u << bit); updateIpl(); }
    bool dma(int bit) const { return (dmacon_ & 0x200) && (dmacon_ & (1u << bit)); }
    uint32_t ptr(int regOff) const { return ((uint32_t(reg_[regOff / 2]) << 16) | reg_[regOff / 2 + 1]) & 0xFFFFFE; }
    // DMA addresses (copper, blitter, bitplanes, sprites, audio). On a real Amiga the DMA sees only chip RAM.
    // On floppy the whole game sits in chip RAM; here the program is loaded into expansion memory, and some
    // data read by the DMA (for example sprite 7 of the copper list) stay there.
    // To behave like the floppy original, the DMA here also reads and writes the expansion memory.
    uint32_t dmaAddr(uint32_t a) const {
        a &= 0xFFFFFE;
        if (a >= kExpBase && a < kExpBase + expSize_) return a;
        return a & chipMask_;
    }
    uint16_t dmaRead(uint32_t a) const { return peek16(dmaAddr(a)); }
    void dmaWrite(uint32_t a, uint16_t v) { poke16(dmaAddr(a), v); }
    void setPtr(int regOff, uint32_t v) { reg_[regOff / 2] = uint16_t(v >> 16); reg_[regOff / 2 + 1] = uint16_t(v); }

    // copper
    uint32_t copPc_ = 0;
    enum class CopState { Run, Wait, Stop } copState_ = CopState::Stop;
    uint16_t copW1_ = 0, copW2_ = 0;
    int copDelay_ = 0;
    bool copSkip_ = false;
    void copperStep(int hpos);
    void copperRestart(uint32_t pc) { copPc_ = pc; copState_ = CopState::Run; copDelay_ = 2; }
    bool copperCompare(int hpos) const;

    // blitter
    bool bzero_ = false;
    void blit();
    void blitLine();
    int blitPending_ = 0;                                    // steps before the end-of-blit interrupt

    // bitplanes
    std::array<std::vector<uint16_t>, 8> lineData_;
    bool lineFetched_ = false, firstBplLine_ = true;
    void fetchBitplanes();
    // sprite
    struct Sprite { uint16_t pos = 0, ctl = 0, data = 0, datb = 0; bool armed = false, dmaActive = false; int vstart = 0, vstop = 0;
                    uint16_t wa[4] = {}, wb[4] = {}; int words = 1; };     // AGA: 1, 2 or 4 words per line (16/32/64 pixels)
    std::array<Sprite, 8> spr_{};
    void spriteLine();
    // colours: value at the start of the line + changes during the line (position in lores pixels)
    std::array<uint32_t, 256> color_{};                       // 0x00RRGGBB (OCS: 12-bit values expanded)
    struct ColorChange { int x; int index; uint32_t value; };
    std::vector<ColorChange> colorLog_;
    std::array<uint32_t, 256> lineStartColor_{};
    uint16_t bplcon0Line_ = 0, bplcon1Line_ = 0, bplcon2Line_ = 0, bplcon3Line_ = 0, bplcon4Line_ = 0;
    bool aga_ = false, wide_ = false;
    uint8_t wideMask_ = 0xFF;
    int wideLimit_ = 0;
    int fetchStrt_ = 0;                                       // DDFSTRT of the line
    uint32_t chipMask_ = kChipSize - 1;
    std::vector<uint32_t> fb_;
    void renderLine();

    // audio
    struct Channel {
        uint32_t pt = 0; uint16_t len = 0; int per = 0; int vol = 0;
        int counter = 0; uint16_t word = 0; int byteIndex = 2; int8_t sample = 0;
        bool active = false; int left = 0; int group = 0;
    };
    std::array<Channel, 4> aud_{};
    int audioRate_ = 44100;
    int audioGroup_ = 0;
    std::array<int, 4> groupGain_{256, 256, 256, 256};
    double audioAccum_ = 0;
    std::vector<int16_t> audioOut_;
    void audioDmaStart(int ch);
    void audioLine();

    // CIA
    Cia ciaA_, ciaB_;
    uint8_t ciaRead(Cia& c, int r, bool isA);
    void ciaWrite(Cia& c, int r, uint8_t v, bool isA);
    void ciaInterrupts();
    // keyboard
    std::deque<uint8_t> keyQueue_;
    int keyCooldown_ = 0, keyTimeout_ = 0;
    bool keyWaitAck_ = false;
    void keyboardLine();

    // input
    struct Port {
        bool mouse = false;
        int mx = 0, my = 0;                         // mouse counters
        uint16_t joy = 0;                           // joystick JOYxDAT
        bool fire = false, fire2 = false;
        uint16_t dat() const;
    };
    Port port_[2] = {Port{true}, Port{}};           // port 1 mouse, port 2 joystick

    // --- game loader (loader.cpp) ---
    std::string saveDir_;
    uint32_t custom1_ = 0;
    uint32_t gameBase_ = 0;
    struct Pool { uint32_t start = 0, end = 0; };
    Pool pools_[2];                                           // AllocMem: chip, fast
    std::string selectedFile_;                                // disk routine: file of the partial reads
    uint32_t announcedPart_ = 0;
    void installGameHooks(uint32_t base);
    std::string readName(uint32_t a) const;
    std::string savePath(const std::string& name) const;
    bool loadFileBytes(const std::string& name, std::vector<uint8_t>& out);
    bool loadFilePart(const std::string& name, uint32_t size, uint32_t offset, uint32_t dest);
    bool saveFilePart(const std::string& name, uint32_t size, uint32_t offset, uint32_t src);
    uint32_t decrunch(uint32_t src, uint32_t dst);
};

} // namespace amiga
