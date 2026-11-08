#include "amiga/machine.h"
#include "amiga/cpu.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace amiga {

// ============================================================================================
// CPU
// ============================================================================================
Cpu::Cpu(Machine& m) : m_(m) {}
moira::u8 Cpu::read8(moira::u32 a) const { return m_.busRead8(a); }
moira::u16 Cpu::read16(moira::u32 a) const { return m_.busRead16(a); }
moira::u32 Cpu::read32(moira::u32 a) const { return (moira::u32(m_.busRead16(a)) << 16) | m_.busRead16(a + 2); }
void Cpu::write8(moira::u32 a, moira::u8 v) const { m_.busWrite8(a, v); }
void Cpu::write16(moira::u32 a, moira::u16 v) const { m_.busWrite16(a, v); }
void Cpu::write32(moira::u32 a, moira::u32 v) const { m_.busWrite16(a, moira::u16(v >> 16)); m_.busWrite16(a + 2, moira::u16(v)); }
void Cpu::cpuDidHalt() { if (m_.log) m_.log("CPU halted (double bus fault)"); }
void Cpu::willExecute(moira::M68kException, moira::u16 vector) {
    if (vector < 64) ++m_.exceptionCount[vector];
    if (m_.log && vector != 0 && (vector < 24 || vector > 31) && vector != 32 + 0)
        m_.log("CPU exception, vector " + std::to_string(vector) + " at PC " + std::to_string(getPC0()));
}

// ============================================================================================
// Construction and memory
// ============================================================================================
Machine::Machine()
    : chip_(kChipSizeAga, 0), private_(kPrivateSize, 0), cpu_(std::make_unique<Cpu>(*this)),
      hookMap_(0x1000000 / 16, 0), fb_(size_t(kFbWidth) * kFbHeight, 0) {
    cpu_->setModel(moira::Model::M68000);
    ciaA_.inputA = [](Machine& m) -> uint8_t {
        uint8_t v = 0xFF;
        if (m.port_[1].fire) v &= uint8_t(~0x80);      // fire button of port 2
        if (m.port_[0].fire) v &= uint8_t(~0x40);      // fire button (left mouse button) of port 1
        return v;
    };
}

Machine::~Machine() = default;

void Machine::setAga(bool on) {
    aga_ = on;
    chipMask_ = (on ? kChipSizeAga : kChipSize) - 1;
    reg_[0x10C / 2] = on ? 0x0011 : 0;           // BPLCON4 after reset: sprite banks at colour 16
}

static inline uint32_t ocsRgb(uint16_t c) {
    const uint32_t r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
    return (r * 17) << 16 | (g * 17) << 8 | (b * 17);
}

uint8_t* Machine::mem(uint32_t a, uint32_t n) {
    a &= 0xFFFFFF;
    if (a < 0x200000) { a &= chipMask_; return a + n <= chipMask_ + 1 ? &chip_[a] : nullptr; }
    if (a >= kExpBase && a + n <= kExpBase + expSize_) return &exp_[a - kExpBase];
    if (a >= kPrivateBase && a + n <= kPrivateBase + kPrivateSize) return &private_[a - kPrivateBase];
    return nullptr;
}
const uint8_t* Machine::mem(uint32_t a, uint32_t n) const { return const_cast<Machine*>(this)->mem(a, n); }

uint8_t Machine::peek8(uint32_t a) const { const uint8_t* p = mem(a, 1); return p ? *p : 0; }
uint16_t Machine::peek16(uint32_t a) const { const uint8_t* p = mem(a, 2); return p ? uint16_t((p[0] << 8) | p[1]) : 0; }
uint32_t Machine::peek32(uint32_t a) const { return (uint32_t(peek16(a)) << 16) | peek16(a + 2); }
void Machine::poke8(uint32_t a, uint8_t v) { if (uint8_t* p = mem(a, 1)) *p = v; }
void Machine::poke16(uint32_t a, uint16_t v) { if (uint8_t* p = mem(a, 2)) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); } }
void Machine::poke32(uint32_t a, uint32_t v) { poke16(a, uint16_t(v >> 16)); poke16(a + 2, uint16_t(v)); }

// --- bus ---
uint8_t Machine::busRead8(uint32_t a) {
    a &= 0xFFFFFF;
    if (const uint8_t* p = mem(a, 1)) return *p;
    if ((a & 0xFFF000) == 0xDFF000) { uint16_t w = customRead(a & 0x1FE); return (a & 1) ? uint8_t(w) : uint8_t(w >> 8); }
    if ((a & 0xFF0000) == 0xBF0000) {
        const int r = (a >> 8) & 0xF;
        if ((a & 1) && !(a & 0x1000)) return ciaRead(ciaA_, r, true);
        if (!(a & 1) && !(a & 0x2000)) return ciaRead(ciaB_, r, false);
        return 0xFF;
    }
    return 0;
}

uint16_t Machine::busRead16(uint32_t a) {
    a &= 0xFFFFFE;
    if (const uint8_t* p = mem(a, 2)) return uint16_t((p[0] << 8) | p[1]);
    if ((a & 0xFFF000) == 0xDFF000) return customRead(a & 0x1FE);
    if ((a & 0xFF0000) == 0xBF0000) return uint16_t((busRead8(a) << 8) | busRead8(a + 1));
    return 0;
}

void Machine::busWrite8(uint32_t a, uint8_t v) {
    a &= 0xFFFFFF;
    if (uint8_t* p = mem(a, 1)) { *p = v; return; }
    if ((a & 0xFFF000) == 0xDFF000) { customWrite(a & 0x1FE, uint16_t((v << 8) | v)); return; }
    if ((a & 0xFF0000) == 0xBF0000) {
        const int r = (a >> 8) & 0xF;
        if ((a & 1) && !(a & 0x1000)) ciaWrite(ciaA_, r, v, true);
        if (!(a & 1) && !(a & 0x2000)) ciaWrite(ciaB_, r, v, false);
    }
}

void Machine::busWrite16(uint32_t a, uint16_t v) {
    a &= 0xFFFFFE;
    if (uint8_t* p = mem(a, 2)) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); return; }
    if ((a & 0xFFF000) == 0xDFF000) { customWrite(a & 0x1FE, v); return; }
    if ((a & 0xFF0000) == 0xBF0000) { busWrite8(a, uint8_t(v >> 8)); busWrite8(a + 1, uint8_t(v)); }
}

// ============================================================================================
// Hooks
// ============================================================================================
void Machine::addHook(uint32_t addr, Hook h) {
    addr &= 0xFFFFFE;
    hooks_[addr] = std::move(h);
    hookMap_[addr >> 4] |= uint8_t(1u << ((addr >> 1) & 7));
}

void Machine::jumpTo(uint32_t addr) {
    cpu_->setPC(addr);
    cpu_->setPC0(addr);
    cpu_->setIRD(busRead16(addr));
    cpu_->setIRC(busRead16(addr + 2));
}

void Machine::returnFromSubroutine() {
    const uint32_t sp = cpu_->getA(7);
    const uint32_t ra = peek32(sp);
    cpu_->setA(7, sp + 4);
    jumpTo(ra);
}

int Machine::hposNow() const {
    const int64_t d = (cpu_->getClock() - lineStart_) / 2;
    return int(std::clamp<int64_t>(d, 0, kCck - 1));
}

// ============================================================================================
// Main loop: one PAL frame
// ============================================================================================
void Machine::runFrame() {
    if (aborted_) return;
    audioOut_.clear();
    for (int line = 0; line < kLines && !aborted_; ++line) {
        vpos_ = line;
        if (line == 0) {
            raise(5);                                   // VERTB
            copperRestart(ptr(0x080));
            ciaA_.tickTod();
            ++frames_;
            firstBplLine_ = true;
        }
        ciaB_.tickTod();
        lineFetched_ = false;
        spriteLine();
        bplcon0Line_ = 0;
        const int ddf = reg_[0x092 / 2] & 0xFC;
        for (int h = 0; h < kCck; h += 4) {
            copperStep(h);
            if (!lineFetched_ && h >= ddf) fetchBitplanes();
            const int64_t target = lineStart_ + int64_t(std::min(h + 4, kCck)) * 2;
            while (cpu_->getClock() < target && !aborted_) {
                const uint32_t pc = cpu_->getPC() & 0xFFFFFE;
                if (hookMap_[pc >> 4] & (1u << ((pc >> 1) & 7))) {
                    auto it = hooks_.find(pc);
                    if (it != hooks_.end()) { it->second(*this); cpu_->setClock(cpu_->getClock() + 4); continue; }
                }
                cpu_->execute();
            }
            if (blitPending_ > 0 && --blitPending_ == 0) raise(6);
            if (onStep) onStep(*this);
        }
        // CIA: E clock = CPU / 10
        ciaA_.eAccum += kCpuCyclesPerLine / 10.0;
        ciaB_.eAccum += kCpuCyclesPerLine / 10.0;
        const int na = int(ciaA_.eAccum), nb = int(ciaB_.eAccum);
        ciaA_.eAccum -= na; ciaB_.eAccum -= nb;
        ciaA_.tickE(na);
        ciaB_.tickE(nb);
        ciaInterrupts();
        keyboardLine();
        audioLine();
        renderLine();
        lineStart_ += kCpuCyclesPerLine;
    }
}

// ============================================================================================
// Interrupt
// ============================================================================================
void Machine::updateIpl() {
    int level = 0;
    if (intena_ & 0x4000) {
        const uint16_t act = intena_ & intreq_ & 0x3FFF;
        static const int lv[14] = {1, 1, 1, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6};
        for (int b = 13; b >= 0; --b) if (act & (1u << b)) { level = lv[b]; break; }
    }
    cpu_->setIPL(moira::u8(level));
}

// ============================================================================================
// Custom registers
// ============================================================================================
uint16_t Machine::customRead(uint32_t off) {
    switch (off) {
        case 0x002: return uint16_t((dmacon_ & 0x07FF) | (bzero_ ? 0x2000 : 0));
        case 0x004: return uint16_t(0x8000 | (aga_ ? 0x2200 : 0) | ((vpos_ >> 8) & 1));   // LOF, Alice PAL id
        case 0x006: return uint16_t(((vpos_ & 0xFF) << 8) | hposNow());
        case 0x00A: return port_[0].dat();
        case 0x00C: return port_[1].dat();
        case 0x010: return adkcon_;
        case 0x012: case 0x014: return 0;
        case 0x016: {
            uint16_t v = 0xFF00;
            const uint16_t potgo = reg_[0x034 / 2];
            // pin 9 of port 2 (second button) and of port 1 (right mouse button)
            if (port_[1].fire2 && !(potgo & 0x8000)) v &= uint16_t(~0x4000);
            if (port_[0].fire2 && !(potgo & 0x0800)) v &= uint16_t(~0x0400);
            // pins set as outputs: the written value is read back
            for (int b = 8; b < 16; b += 2) if (potgo & (1u << (b + 1))) v = uint16_t((v & ~(1u << b)) | (potgo & (1u << b)));
            return v;
        }
        case 0x018: return 0x3000;
        case 0x01A: return 0;
        case 0x01C: return intena_;
        case 0x01E: return intreq_;
        case 0x07C: return aga_ ? 0x00F8 : 0xFFFF;      // Lisa id (AGA) / no ECS Denise
        default: return 0;
    }
}

void Machine::customWrite(uint32_t off, uint16_t v) {
    off &= 0x1FE;
    auto setclr = [&](uint16_t& r, uint16_t mask) {
        if (v & 0x8000) r = uint16_t(r | (v & mask)); else r = uint16_t(r & ~(v & mask));
    };
    if (off >= 0x180 && off < 0x1C0) {
        int i = int(off - 0x180) / 2;
        uint32_t rgb = ocsRgb(v);
        if (aga_) {
            const uint16_t con3 = reg_[0x106 / 2];
            i += ((con3 >> 13) & 7) * 32;                                    // BANK
            if (con3 & 0x200) {                                              // LOCT: low nibbles only
                const uint32_t lo = ((v >> 8) & 15u) << 16 | ((v >> 4) & 15u) << 8 | (v & 15u);
                rgb = (color_[size_t(i)] & 0xF0F0F0) | lo;
            }
        }
        color_[size_t(i)] = rgb;
        colorLog_.push_back({hposNow() * 2 + 1, i, rgb});
        reg_[off / 2] = v;
        return;
    }
    reg_[off / 2] = v;
    if (off >= 0x0A0 && off < 0x0E0 && ((off & 0xF) == 0x6 || (off & 0xF) == 0x8)) aud_[(off - 0xA0) / 16].group = audioGroup_;
    switch (off) {
        case 0x058: blit(); break;
        case 0x088: copperRestart(ptr(0x080)); break;
        case 0x08A: copperRestart(ptr(0x084)); break;
        case 0x096: {
            const uint16_t old = dmacon_;
            setclr(dmacon_, 0x07FF);
            for (int c = 0; c < 4; ++c) {
                const bool was = (old & 0x200) && (old & (1u << c)), now = dma(c);
                if (!was && now) audioDmaStart(c);
                if (was && !now) aud_[size_t(c)].active = false;
            }
            break;
        }
        case 0x09A: setclr(intena_, 0x7FFF); updateIpl(); break;
        case 0x09C: setclr(intreq_, 0x7FFF); updateIpl(); break;
        case 0x09E: setclr(adkcon_, 0x7FFF); break;
        case 0x0A4: case 0x0B4: case 0x0C4: case 0x0D4: aud_[(off - 0xA0) / 16].len = v; break;
        case 0x0A6: case 0x0B6: case 0x0C6: case 0x0D6: aud_[(off - 0xA0) / 16].per = v; break;
        case 0x0A8: case 0x0B8: case 0x0C8: case 0x0D8: aud_[(off - 0xA0) / 16].vol = std::min<int>(v & 0x7F, 64); break;
        case 0x140: case 0x148: case 0x150: case 0x158: case 0x160: case 0x168: case 0x170: case 0x178: {
            Sprite& s = spr_[(off - 0x140) / 8]; s.pos = v; break;
        }
        case 0x142: case 0x14A: case 0x152: case 0x15A: case 0x162: case 0x16A: case 0x172: case 0x17A: {
            Sprite& s = spr_[(off - 0x142) / 8]; s.ctl = v; s.armed = false; break;
        }
        case 0x144: case 0x14C: case 0x154: case 0x15C: case 0x164: case 0x16C: case 0x174: case 0x17C: {
            Sprite& s = spr_[(off - 0x144) / 8]; s.data = v; s.wa[0] = v; s.words = 1; s.armed = true; break;
        }
        case 0x146: case 0x14E: case 0x156: case 0x15E: case 0x166: case 0x16E: case 0x176: case 0x17E: {
            Sprite& s = spr_[(off - 0x146) / 8]; s.datb = v; s.wb[0] = v; break;
        }
        default: break;
    }
}

// ============================================================================================
// Copper
// ============================================================================================
bool Machine::copperCompare(int hpos) const {
    const int vp = copW1_ >> 8, hp = copW1_ & 0xFE;
    const int ve = ((copW2_ >> 8) & 0x7F) | 0x80, he = copW2_ & 0xFE;
    const int v = vpos_ & 0xFF;
    if ((v & ve) > (vp & ve)) return true;
    if ((v & ve) < (vp & ve)) return false;
    return (hpos & he) >= (hp & he);
}

void Machine::copperStep(int hpos) {
    if (!dma(7)) return;
    if (copState_ == CopState::Stop) return;
    if (copDelay_ > 0) { --copDelay_; return; }
    if (copState_ == CopState::Wait) {
        if (!copperCompare(hpos)) return;
        copState_ = CopState::Run;
        return;
    }
    // at most two instructions per step of 4 colour clocks (a MOVE takes 4 cck)
    copW1_ = dmaRead(copPc_);
    copW2_ = dmaRead((copPc_ + 2));
    copPc_ += 4;
    if (copSkip_) { copSkip_ = false; return; }
    if (!(copW1_ & 1)) {                                   // MOVE
        const uint32_t r = copW1_ & 0x1FE;
        const bool cdang = reg_[0x02E / 2] & 2;
        if (r < 0x40 || (r < 0x80 && !cdang)) { copState_ = CopState::Stop; return; }
        customWrite(r, copW2_);
        return;
    }
    if (!(copW2_ & 1)) {                                   // WAIT
        if (copW1_ == 0xFFFF && copW2_ == 0xFFFE) { copState_ = CopState::Stop; return; }
        if (!copperCompare(hpos)) copState_ = CopState::Wait;
        return;
    }
    if (copperCompare(hpos)) copSkip_ = true;              // SKIP
}

// ============================================================================================
// Bitplanes and sprites
// ============================================================================================
void Machine::fetchBitplanes() {
    lineFetched_ = true;
    const uint16_t con0 = reg_[0x100 / 2];
    int np = (con0 >> 12) & 7;
    if (aga_) { if (con0 & 0x10) np = 8; } else if (np > 6) np = 6;
    const int vstart = reg_[0x08E / 2] >> 8;
    const uint16_t ds = reg_[0x090 / 2];
    const int vstop = (ds >> 8) | ((ds & 0x8000) ? 0 : 0x100);
    if (!dma(8) || np == 0 || vpos_ < vstart || vpos_ >= vstop) { for (auto& d : lineData_) d.clear(); return; }
    const bool hires = con0 & 0x8000;
    const int strt = reg_[0x092 / 2] & 0xFC, stop = reg_[0x094 / 2] & 0xFC;
    int words = hires ? (stop - strt) / 4 + 2 : (stop - strt) / 8 + 1;
    const int fm = aga_ ? (reg_[0x1FC / 2] & 3) : 0;
    fetchStrt_ = strt;
    if (fm) {                                    // AGA FMODE: fetch units of 16 (2x) or 32 (4x) colour clocks from
        const int sh = fm == 3 ? 2 : 1, unit = 8 << sh;   // DDFSTRT, and one more unit after the one that passes DDFSTOP
        words = ((stop - strt) / unit + 2) << (sh + (hires ? 1 : 0));
    }
    words = std::clamp(words, 0, 128);
    bplcon0Line_ = con0;
    bplcon1Line_ = reg_[0x102 / 2];
    bplcon2Line_ = reg_[0x104 / 2];
    bplcon3Line_ = reg_[0x106 / 2];
    bplcon4Line_ = reg_[0x10C / 2];
    if (firstBplLine_) {
        firstBpl1pt = ptr(0x0E0);
        for (int p = 0; p < 8; ++p) firstBplPt[p] = ptr(0x0E0 + p * 4);
        firstBplCon0 = con0;
        firstBplLine_ = false;
    }
    for (int p = 0; p < 8; ++p) {
        auto& d = lineData_[size_t(p)];
        d.clear();
        if (p >= np) continue;
        uint32_t a = ptr(0x0E0 + p * 4);
        d.resize(size_t(words));
        for (int w = 0; w < words; ++w, a += 2) d[size_t(w)] = dmaRead(a);
        const int16_t mod = int16_t(reg_[(p & 1) ? 0x10A / 2 : 0x108 / 2]);
        a = uint32_t(int32_t(a) + mod);
        setPtr(0x0E0 + p * 4, a & 0xFFFFFE);
    }
}

void Machine::spriteLine() {
    if (!dma(5) || vpos_ < 25) return;
    // AGA FMODE bits 3-2: sprite fetch width (16, 32 or 64 bits); control words then sit at the start of each unit
    const int sw = aga_ ? ((reg_[0x1FC / 2] >> 2) & 3) : 0;
    const int nw = sw == 0 ? 1 : (sw == 3 ? 4 : 2);
    const uint32_t unit = uint32_t(nw) * 2;
    for (int i = 0; i < 8; ++i) {
        Sprite& s = spr_[size_t(i)];
        uint32_t a = ptr(0x120 + i * 4);
        auto ctrl = [&] {
            s.pos = dmaRead(a); s.ctl = dmaRead(a + unit); a += 2 * unit;
            s.armed = false; s.dmaActive = false;
            s.vstart = (s.pos >> 8) | ((s.ctl & 4) << 6);
            s.vstop = (s.ctl >> 8) | ((s.ctl & 2) << 7);
        };
        if (vpos_ == 25) { ctrl(); }
        else {
            if (!s.dmaActive && vpos_ == s.vstart && s.vstop > s.vstart) s.dmaActive = true;
            if (s.dmaActive) {
                if (vpos_ >= s.vstop) { ctrl(); if (vpos_ == s.vstart && s.vstop > s.vstart) s.dmaActive = true; }
                if (s.dmaActive) {
                    for (int w = 0; w < nw; ++w) { s.wa[w] = dmaRead(a + 2 * w); s.wb[w] = dmaRead(a + unit + 2 * w); }
                    s.data = s.wa[0]; s.datb = s.wb[0]; s.words = nw;
                    a += 2 * unit; s.armed = true;
                }
            }
        }
        setPtr(0x120 + i * 4, a & 0xFFFFFE);
    }
}

std::string Machine::debugSprites() const {
    std::string o = "dmacon " + std::to_string(dmacon_) + "\n";
    char b[160];
    for (int i = 0; i < 8; ++i) {
        const Sprite& s = spr_[size_t(i)];
        std::snprintf(b, sizeof b, "spr%d pt=%05X pos=%04X ctl=%04X data=%04X datb=%04X armed=%d dma=%d v=%d..%d\n", i, ptr(0x120 + i * 4), s.pos, s.ctl, s.data, s.datb, s.armed, s.dmaActive, s.vstart, s.vstop);
        o += b;
    }
    return o;
}

void Machine::renderLine() {
    const int y = vpos_ - kFbFirstLine;
    if (y < 0 || y >= kFbHeight) { lineStartColor_ = color_; colorLog_.clear(); return; }
    uint32_t* out = &fb_[size_t(y) * kFbWidth];
    std::array<uint32_t, 256> col = lineStartColor_;
    size_t ci = 0;

    const uint16_t con0 = bplcon0Line_;
    int np = (con0 >> 12) & 7;
    if (aga_) { if (con0 & 0x10) np = 8; } else if (np > 6) np = 6;
    const bool hires = con0 & 0x8000, dpf = con0 & 0x400;
    const bool ham6 = (con0 & 0x800) && np == 6, ham8 = aga_ && (con0 & 0x800) && np == 8, ham = ham6 || ham8;
    const bool killEhb = aga_ && (bplcon2Line_ & 0x200);
    const bool ehb = np == 6 && !ham && !dpf && !killEhb;
    const int vstart = reg_[0x08E / 2] >> 8;
    const uint16_t ds = reg_[0x090 / 2];
    const int vstop = (ds >> 8) | ((ds & 0x8000) ? 0 : 0x100);
    const bool vwin = vpos_ >= vstart && vpos_ < vstop;
    const int hstart = reg_[0x08E / 2] & 0xFF, hstop = (ds & 0xFF) | 0x100;
    const int strt = aga_ ? fetchStrt_ : reg_[0x092 / 2] & 0xFC;
    // position (in half lores pixels) of the first fetched pixel
    const int x0h = hires ? (strt * 2 + 9) * 2 : (strt * 2 + 17) * 2;
    const int s1 = bplcon1Line_ & 15, s2 = (bplcon1Line_ >> 4) & 15;
    int d1 = s1 * 2, d2 = s2 * 2;
    if (aga_) {                                  // AGA scroll: PF1H7-6 bits 11-10, PF2H7-6 bits 15-14, H1 = half pixel
        d1 += ((bplcon1Line_ >> 10) & 3) * 32 + ((bplcon1Line_ >> 9) & 1);
        d2 += ((bplcon1Line_ >> 14) & 3) * 32 + ((bplcon1Line_ >> 13) & 1);
    }
    const int words = lineData_[0].empty() ? 0 : int(lineData_[0].size());
    // wide window: from the first to the last fetched pixel (lores coordinates)
    const int wideL = x0h / 2, wideR = x0h / 2 + (hires ? words * 8 : words * 16);
    const int pf1p = bplcon2Line_ & 7, pf2p = (bplcon2Line_ >> 3) & 7;
    const bool pf2pri = bplcon2Line_ & 0x40;
    // AGA: bitplane XOR (BPLAM), playfield 2 colour offset (PF2OF), sprite colour banks (ESPRM/OSPRM)
    const int bplam = aga_ ? (bplcon4Line_ >> 8) : 0;
    static const int pf2ofTab[8] = {0, 2, 4, 8, 16, 32, 64, 128};
    const int pf2of = aga_ ? pf2ofTab[(bplcon3Line_ >> 10) & 7] : 8;
    const int esprBase = aga_ ? (bplcon4Line_ & 15) * 16 : 16, osprBase = aga_ ? ((bplcon4Line_ >> 4) & 15) * 16 : 16;

    // sprites active on this line
    // sprites active on this line: position in half lores pixels (AGA: SH1 in bit 4 of CTL), width in pixels
    struct Spx { int hs; const Sprite* s; int idx; bool attached; const Sprite* s2; };
    Spx sp[8]; int nsp = 0;
    const int spres = aga_ ? (bplcon3Line_ >> 6) & 3 : 0;          // 0/1 lores, 2 hires, 3 super-hires
    const int spxHalf = spres == 2 ? 1 : (spres == 3 ? 1 : 2);     // half lores pixels per sprite pixel
    for (int i = 0; i < 8; ++i) {
        const Sprite& s = spr_[size_t(i)];
        if (!s.armed) continue;
        int hs = (((s.pos & 0xFF) << 1) | (s.ctl & 1)) * 2;
        if (aga_) hs += (s.ctl >> 4) & 1;
        sp[nsp++] = {hs, &s, i, false, nullptr};
    }
    for (int k = 0; k < nsp; ++k) {             // attached pairs: the odd sprite has bit 7 of CTL
        const int i = sp[k].idx;
        if ((i & 1) && (spr_[size_t(i)].ctl & 0x80)) {
            for (int j = 0; j < nsp; ++j) if (sp[j].idx == i - 1) { sp[j].attached = true; sp[j].s2 = sp[k].s; sp[k].idx = -1; }
        }
    }

    auto halfOcs = [](uint32_t c) {             // OCS EHB: each 4-bit component halved
        const uint32_t r = ((c >> 16) & 255) / 17, g = ((c >> 8) & 255) / 17, b = (c & 255) / 17;
        return ((r >> 1) * 17) << 16 | ((g >> 1) * 17) << 8 | ((b >> 1) * 17);
    };
    uint32_t hamPrev = col[0];
    for (int xh = 0; xh < kFbWidth; ++xh) {
        const int X = kFbFirstLores + xh / 2;               // lores coordinate of the beam
        while (ci < colorLog_.size() && colorLog_[ci].x <= X) { col[size_t(colorLog_[ci].index)] = colorLog_[ci].value; ++ci; }
        const bool inDiw = vwin && X >= hstart && X < hstop;
        const bool inWin = inDiw || (wide_ && vwin && words && X >= wideL && X < wideR);
        int pix = 0; bool pfOpaque = false; uint32_t rgb;
        int v1 = 0, v2 = 0;
        if (inWin && words) {
            const int hx = kFbFirstLores * 2 + xh;          // half lores pixels
            for (int p = 0; p < np; ++p) {
                const int del = (p & 1) ? d2 : d1;
                int i = hires ? (hx - x0h - del) : (hx - x0h - del) / 2;
                if (!hires && (hx - x0h - del) < 0) i = -1;
                if (i < 0 || i >= words * 16) continue;
                if (!inDiw && !(wideMask_ & (1 << p)) && i >= wideLimit_) continue;
                if ((lineData_[size_t(p)][size_t(i >> 4)] >> (15 - (i & 15))) & 1) pix |= 1 << p;
            }
            if (dpf) {
                v1 = (pix & 1) | ((pix >> 1) & 2) | ((pix >> 2) & 4) | ((pix >> 3) & 8);
                v2 = ((pix >> 1) & 1) | ((pix >> 2) & 2) | ((pix >> 3) & 4) | ((pix >> 4) & 8);
                pfOpaque = v1 || v2;
            } else pfOpaque = pix != 0;
        }
        // playfield colour
        if (!inWin) rgb = col[0];
        else if (ham6) {
            const int c = pix & 15, cx = c * 17;
            switch (pix >> 4) {
                case 0: hamPrev = col[size_t(c ^ (bplam & 15))]; break;
                case 1: hamPrev = (hamPrev & 0xFFFF00) | uint32_t(cx); break;
                case 2: hamPrev = (hamPrev & 0x00FFFF) | uint32_t(cx) << 16; break;
                default: hamPrev = (hamPrev & 0xFF00FF) | uint32_t(cx) << 8; break;
            }
            rgb = hamPrev;
        } else if (ham8) {                                   // control = planes 1-2, data = planes 3-8
            const int c = pix >> 2;
            switch (pix & 3) {
                case 0: hamPrev = col[size_t(c ^ (bplam >> 2))]; break;
                case 1: hamPrev = (hamPrev & 0xFFFF00) | uint32_t(c << 2 | (hamPrev & 3)); break;
                case 2: hamPrev = (hamPrev & 0x00FFFF) | uint32_t(c << 2 | ((hamPrev >> 16) & 3)) << 16; break;
                default: hamPrev = (hamPrev & 0xFF00FF) | uint32_t(c << 2 | ((hamPrev >> 8) & 3)) << 8; break;
            }
            rgb = hamPrev;
        } else if (dpf) {
            const bool front2 = pf2pri;
            int idx = 0;
            if (front2) idx = v2 ? pf2of + v2 : (v1 ? v1 : 0);
            else idx = v1 ? v1 : (v2 ? pf2of + v2 : 0);
            rgb = col[size_t((idx ^ bplam) & 255)];
        } else if (ehb && (pix & 32)) {
            const uint32_t c = col[size_t((pix ^ bplam) & 31)];
            rgb = aga_ ? (c >> 1) & 0x7F7F7F : halfOcs(c);
        } else rgb = col[size_t((pix ^ bplam) & (aga_ ? 255 : 31))];
        if (!inWin) hamPrev = col[0];

        // sprites (inside the window)
        if (inWin && nsp && !noSprites_) {
            for (int k = 0; k < nsp; ++k) {
                if (sp[k].idx < 0) continue;
                const Sprite& S = *sp[k].s;
                const int dh = kFbFirstLores * 2 + (aga_ ? xh : (xh & ~1)) - sp[k].hs;
                if (dh < 0) continue;
                const int off = dh / spxHalf;
                if (off >= 16 * S.words) continue;
                const int w = off >> 4, bit = 15 - (off & 15);
                int v = ((S.wa[w] >> bit) & 1) | (((S.wb[w] >> bit) & 1) << 1);
                int cidx;
                if (sp[k].attached) {
                    const Sprite& S2 = *sp[k].s2;
                    v |= (((S2.wa[w] >> bit) & 1) << 2) | (((S2.wb[w] >> bit) & 1) << 3);
                    if (!v) continue;
                    cidx = osprBase + v;
                } else {
                    if (!v) continue;
                    cidx = ((sp[k].idx & 1) ? osprBase : esprBase) + (sp[k].idx >> 1) * 4 + v;
                }
                const int pair = sp[k].idx >> 1;
                bool front;
                if (!pfOpaque) front = true;
                else if (dpf) front = (v1 ? pair < pf1p : true) && (v2 ? pair < pf2p : true);
                else front = pair < pf2p;
                if (front) rgb = col[size_t(cidx & 255)];
                break;
            }
        }
        out[xh] = rgb;
    }
    lineStartColor_ = color_;
    colorLog_.clear();
}

// ============================================================================================
// Audio (Paula, DMA only)
// ============================================================================================
void Machine::audioDmaStart(int c) {
    Channel& ch = aud_[size_t(c)];
    ch.pt = ptr(0x0A0 + c * 16);
    ch.left = ch.len ? ch.len : 0x10000;
    ch.counter = std::max(ch.per, 124);
    ch.byteIndex = 2;
    ch.active = true;
    raise(7 + c);
}

void Machine::audioLine() {
    constexpr double kPaulaClock = 3546895.0;
    const double cckPerSample = kPaulaClock / audioRate_;
    audioAccum_ += double(kCck) / cckPerSample;
    int n = int(audioAccum_);
    audioAccum_ -= n;
    for (int s = 0; s < n; ++s) {
        int l = 0, r = 0;
        for (int c = 0; c < 4; ++c) {
            Channel& ch = aud_[size_t(c)];
            if (ch.active) {
                ch.counter -= int(cckPerSample + 0.5);
                const int per = std::max(ch.per, 124);
                while (ch.counter <= 0) {
                    ch.counter += per;
                    if (ch.byteIndex >= 2) {
                        ch.word = dmaRead(ch.pt);
                        ch.pt += 2;
                        if (--ch.left <= 0) {
                            ch.pt = ptr(0x0A0 + c * 16);
                            ch.left = ch.len ? ch.len : 0x10000;
                            raise(7 + c);
                        }
                        ch.byteIndex = 0;
                    }
                    ch.sample = int8_t(ch.byteIndex == 0 ? (ch.word >> 8) : (ch.word & 0xFF));
                    ++ch.byteIndex;
                }
            }
            const int v = ch.active ? ch.sample * ch.vol * groupGain_[size_t(ch.group)] / 256 : 0;
            if (c == 0 || c == 3) l += v; else r += v;
        }
        // some mixing between the channels: the full stereo separation of the Amiga is tiring on headphones
        const int L = (l * 3 + r) / 4, R = (r * 3 + l) / 4;
        audioOut_.push_back(int16_t(std::clamp(L * 2, -32768, 32767)));
        audioOut_.push_back(int16_t(std::clamp(R * 2, -32768, 32767)));
    }
}

// ============================================================================================
// CIA
// ============================================================================================
bool Cia::tickE(int n) {
    const uint8_t before = icrData;
    int underA = 0;
    if (cra & 1) {
        int t = ta;
        t -= n;
        while (t < 0) {
            ++underA;
            icrData |= 1;
            if (cra & 8) { cra &= uint8_t(~1); t = taLatch; break; }
            t += taLatch + 1;
        }
        ta = uint16_t(t);
    }
    if (crb & 1) {
        const int mode = (crb >> 5) & 3;
        const int cnt = mode == 0 ? n : (mode == 2 ? underA : 0);
        if (cnt) {
            int t = tb;
            t -= cnt;
            while (t < 0) {
                icrData |= 2;
                if (crb & 8) { crb &= uint8_t(~1); t = tbLatch; break; }
                t += tbLatch + 1;
            }
            tb = uint16_t(t);
        }
    }
    return (icrData & ~before & icrMask) != 0;
}

void Cia::tickTod() {
    if (todStopped) return;
    tod = (tod + 1) & 0xFFFFFF;
    if (tod == todAlarm) icrData |= 4;
}

uint8_t Machine::ciaRead(Cia& c, int r, bool isA) {
    switch (r) {
        case 0: {
            const uint8_t in = (isA && c.inputA) ? c.inputA(*this) : 0xFF;
            return uint8_t((c.pra & c.ddra) | (in & ~c.ddra));
        }
        case 1: return uint8_t((c.prb & c.ddrb) | (0xFF & ~c.ddrb));
        case 2: return c.ddra;
        case 3: return c.ddrb;
        case 4: return uint8_t(c.ta);
        case 5: return uint8_t(c.ta >> 8);
        case 6: return uint8_t(c.tb);
        case 7: return uint8_t(c.tb >> 8);
        case 8: { const uint32_t t = c.todLatched ? c.todLatch : c.tod; c.todLatched = false; return uint8_t(t); }
        case 9: return uint8_t((c.todLatched ? c.todLatch : c.tod) >> 8);
        case 10: c.todLatch = c.tod; c.todLatched = true; return uint8_t(c.tod >> 16);
        case 12: return c.sdr;
        case 13: {
            uint8_t v = c.icrData;
            if (c.irqPending()) v |= 0x80;
            c.icrData = 0;
            return v;
        }
        case 14: return c.cra;
        case 15: return c.crb;
        default: return 0xFF;
    }
}

void Machine::ciaWrite(Cia& c, int r, uint8_t v, bool isA) {
    switch (r) {
        case 0: c.pra = v; break;
        case 1: c.prb = v; break;
        case 2: c.ddra = v; break;
        case 3: c.ddrb = v; break;
        case 4: c.taLatch = uint16_t((c.taLatch & 0xFF00) | v); break;
        case 5:
            c.taLatch = uint16_t((c.taLatch & 0x00FF) | (v << 8));
            if (!(c.cra & 1)) c.ta = c.taLatch;
            if (c.cra & 8) { c.ta = c.taLatch; c.cra |= 1; }
            break;
        case 6: c.tbLatch = uint16_t((c.tbLatch & 0xFF00) | v); break;
        case 7:
            c.tbLatch = uint16_t((c.tbLatch & 0x00FF) | (v << 8));
            if (!(c.crb & 1)) c.tb = c.tbLatch;
            if (c.crb & 8) { c.tb = c.tbLatch; c.crb |= 1; }
            break;
        case 8: case 9: case 10: {
            uint32_t& t = (c.crb & 0x80) ? c.todAlarm : c.tod;
            const int sh = (r - 8) * 8;
            t = (t & ~(0xFFu << sh)) | (uint32_t(v) << sh);
            if (!(c.crb & 0x80)) c.todStopped = (r == 10) ? true : (r == 8 ? false : c.todStopped);
            break;
        }
        case 12: c.sdr = v; break;
        case 13:
            if (v & 0x80) c.icrMask |= (v & 0x1F); else c.icrMask &= uint8_t(~(v & 0x1F));
            break;
        case 14: {
            const uint8_t old = c.cra;
            c.cra = uint8_t(v & ~0x10);
            if (v & 0x10) c.ta = c.taLatch;
            // end of the keyboard handshake (SPMODE from output to input)
            if (isA && (old & 0x40) && !(v & 0x40)) { keyWaitAck_ = false; keyCooldown_ = 2; }
            break;
        }
        case 15:
            c.crb = uint8_t(v & ~0x10);
            if (v & 0x10) c.tb = c.tbLatch;
            break;
        default: break;
    }
    ciaInterrupts();
}

void Machine::ciaInterrupts() {
    if (ciaA_.irqPending() && !(intreq_ & 0x0008)) raise(3);
    if (ciaB_.irqPending() && !(intreq_ & 0x2000)) raise(13);
}

// ============================================================================================
// Keyboard and input
// ============================================================================================
void Machine::keyEvent(uint8_t raw, bool down) { keyQueue_.push_back(uint8_t((raw & 0x7F) | (down ? 0 : 0x80))); }

void Machine::keyboardLine() {
    if (keyWaitAck_) {                                     // the game has not acknowledged the key yet
        if (++keyTimeout_ < 313) return;
        keyWaitAck_ = false;
    }
    if (keyCooldown_ > 0) { --keyCooldown_; return; }
    if (keyQueue_.empty()) return;
    const uint8_t k = keyQueue_.front();
    keyQueue_.pop_front();
    ciaA_.sdr = uint8_t(~((k << 1) | (k >> 7)));
    ciaA_.setIcr(8);
    keyWaitAck_ = true;
    keyTimeout_ = 0;
    ciaInterrupts();
}

uint16_t Machine::Port::dat() const {
    if (mouse) return uint16_t(((my & 0xFF) << 8) | (mx & 0xFF));
    return joy;
}

void Machine::setPortDevice(int port, PortDevice d) {
    if (port < 0 || port > 1) return;
    Port& p = port_[port];
    p.mouse = d == PortDevice::Mouse;
    p.joy = 0; p.fire = p.fire2 = false;
}

void Machine::setPortJoystick(int port, bool up, bool down, bool left, bool right, bool fire, bool fire2) {
    if (port < 0 || port > 1) return;
    Port& p = port_[port];
    if (p.mouse) return;
    uint16_t v = 0;                                 // JOYxDAT: bits 1/9 right/left, bits 0/8 = down^right, up^left
    if (right) v |= 0x0002;
    if (left) v |= 0x0200;
    if (down ^ right) v |= 0x0001;
    if (up ^ left) v |= 0x0100;
    p.joy = v;
    p.fire = fire;
    p.fire2 = fire2;
}

void Machine::setPortMouse(int port, int dx, int dy, bool left, bool right) {
    if (port < 0 || port > 1) return;
    Port& p = port_[port];
    if (!p.mouse) return;
    p.mx = (p.mx + dx) & 0xFF;
    p.my = (p.my + dy) & 0xFF;
    p.fire = left;
    p.fire2 = right;
}

void Machine::setJoystick(bool up, bool down, bool left, bool right, bool fire, bool fire2) {
    setPortJoystick(1, up, down, left, right, fire, fire2);
}

void Machine::mouseMove(int dx, int dy) { setPortMouse(0, dx, dy, port_[0].fire, port_[0].fire2); }
void Machine::setMouseButtons(bool left, bool right) { setPortMouse(0, 0, 0, left, right); }

} // namespace amiga
