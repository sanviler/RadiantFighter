// OCS blitter: area mode (channels A, B, C, D, shifts, masks, descending, fill)
// and line mode. Executed at once when BLTSIZE is written; the end interrupt arrives
// at the next step.
#include "amiga/machine.h"

namespace amiga {

static inline uint16_t minterm(uint16_t a, uint16_t b, uint16_t c, uint8_t lf) {
    uint16_t d = 0;
    if (lf & 0x01) d |= uint16_t(~a & ~b & ~c);
    if (lf & 0x02) d |= uint16_t(~a & ~b & c);
    if (lf & 0x04) d |= uint16_t(~a & b & ~c);
    if (lf & 0x08) d |= uint16_t(~a & b & c);
    if (lf & 0x10) d |= uint16_t(a & ~b & ~c);
    if (lf & 0x20) d |= uint16_t(a & ~b & c);
    if (lf & 0x40) d |= uint16_t(a & b & ~c);
    if (lf & 0x80) d |= uint16_t(a & b & c);
    return d;
}

void Machine::blit() {
    if (onBlit) onBlit(*this);
    const uint16_t con1 = reg_[0x042 / 2];
    if (con1 & 1) { blitLine(); blitPending_ = 1; return; }
    const uint16_t con0 = reg_[0x040 / 2];
    const uint16_t size = reg_[0x058 / 2];
    int h = size >> 6, w = size & 0x3F;
    if (!h) h = 1024;
    if (!w) w = 64;
    const int ash = con0 >> 12, bsh = con1 >> 12;
    const bool useA = con0 & 0x800, useB = con0 & 0x400, useC = con0 & 0x200, useD = con0 & 0x100;
    const uint8_t lf = uint8_t(con0);
    const bool desc = con1 & 2;
    const bool fill = con1 & 0x18, ife = con1 & 0x08, fci = con1 & 0x04;
    const uint16_t afwm = reg_[0x044 / 2], alwm = reg_[0x046 / 2];
    uint32_t apt = ptr(0x050), bpt = ptr(0x04C), cpt = ptr(0x048), dpt = ptr(0x054);
    const int16_t amod = int16_t(reg_[0x064 / 2] & 0xFFFE), bmod = int16_t(reg_[0x062 / 2] & 0xFFFE);
    const int16_t cmod = int16_t(reg_[0x060 / 2] & 0xFFFE), dmod = int16_t(reg_[0x066 / 2] & 0xFFFE);
    const int inc = desc ? -2 : 2;
    uint16_t aold = 0, bold = 0;
    uint16_t adat = reg_[0x074 / 2], bdat = reg_[0x072 / 2], cdat = reg_[0x070 / 2];
    bzero_ = true;
    for (int y = 0; y < h; ++y) {
        bool carry = fci;
        for (int x = 0; x < w; ++x) {
            if (useA) { adat = dmaRead(apt); apt += uint32_t(inc); }
            if (useB) { bdat = dmaRead(bpt); bpt += uint32_t(inc); }
            if (useC) { cdat = dmaRead(cpt); cpt += uint32_t(inc); }
            uint16_t am = adat;
            if (x == 0) am &= afwm;
            if (x == w - 1) am &= alwm;
            uint16_t as, bs;
            if (!desc) {
                as = uint16_t(((uint32_t(aold) << 16) | am) >> ash);
                bs = uint16_t(((uint32_t(bold) << 16) | bdat) >> bsh);
            } else {
                as = uint16_t(((uint32_t(am) << 16) | aold) >> (16 - ash));
                bs = uint16_t(((uint32_t(bdat) << 16) | bold) >> (16 - bsh));
            }
            aold = am;
            bold = bdat;
            uint16_t d = minterm(as, bs, cdat, lf);
            if (fill && desc) {
                uint16_t o = 0;
                for (int i = 0; i < 16; ++i) {
                    const bool bit = (d >> i) & 1;
                    const bool ob = ife ? (carry || bit) : (carry != bit);
                    if (ob) o |= uint16_t(1u << i);
                    if (bit) carry = !carry;
                }
                d = o;
            }
            if (d) bzero_ = false;
            if (useD) { dmaWrite(dpt, d); dpt += uint32_t(inc); }
        }
        if (desc) {
            if (useA) apt -= uint32_t(amod);
            if (useB) bpt -= uint32_t(bmod);
            if (useC) cpt -= uint32_t(cmod);
            if (useD) dpt -= uint32_t(dmod);
        } else {
            if (useA) apt += uint32_t(amod);
            if (useB) bpt += uint32_t(bmod);
            if (useC) cpt += uint32_t(cmod);
            if (useD) dpt += uint32_t(dmod);
        }
    }
    setPtr(0x050, apt); setPtr(0x04C, bpt); setPtr(0x048, cpt); setPtr(0x054, dpt);
    reg_[0x074 / 2] = adat; reg_[0x072 / 2] = bdat; reg_[0x070 / 2] = cdat;
    blitPending_ = 1;
}

void Machine::blitLine() {
    const uint16_t con0 = reg_[0x040 / 2], con1 = reg_[0x042 / 2];
    int len = reg_[0x058 / 2] >> 6;
    if (!len) len = 1024;
    int shift = con0 >> 12;
    const uint8_t lf = uint8_t(con0);
    const uint16_t adat = reg_[0x074 / 2];
    const int bsh = con1 >> 12;
    uint16_t tex = reg_[0x072 / 2];
    tex = uint16_t((tex >> bsh) | (tex << (16 - bsh)));
    bool sign = con1 & 0x40;
    const bool sing = con1 & 2;
    int32_t err = int16_t(reg_[0x052 / 2]);
    const int16_t amod = int16_t(reg_[0x064 / 2]), bmod = int16_t(reg_[0x062 / 2]);
    const int16_t cmod = int16_t(reg_[0x060 / 2]);
    uint32_t cpt = ptr(0x048), dpt = ptr(0x054);
    bool oneDot = false;
    bzero_ = true;
    auto incx = [&] { if (++shift == 16) { shift = 0; cpt += 2; } };
    auto decx = [&] { if (shift-- == 0) { shift = 15; cpt -= 2; } };
    auto incy = [&] { cpt += uint32_t(cmod); oneDot = false; };
    auto decy = [&] { cpt -= uint32_t(cmod); oneDot = false; };
    for (int i = 0; i < len; ++i) {
        const uint16_t cdat = dmaRead(cpt);
        uint16_t a = uint16_t(adat >> shift);
        if (sing && oneDot) a = 0;
        oneDot = true;
        const uint16_t b = (tex & 1) ? 0xFFFF : 0;
        const uint16_t d = minterm(a, b, cdat, lf);
        if (d) bzero_ = false;
        // step: error term, then a step on the major axis and, if needed, on the minor one
        if (con0 & 0x800) err += sign ? bmod : amod;
        if (!sign) {
            if (con1 & 0x10) { if (con1 & 0x08) decy(); else incy(); }
            else { if (con1 & 0x08) decx(); else incx(); }
        }
        if (con1 & 0x10) { if (con1 & 0x04) decx(); else incx(); }
        else { if (con1 & 0x04) decy(); else incy(); }
        sign = int16_t(err) < 0;
        tex = uint16_t((tex << 1) | (tex >> 15));
        dmaWrite(dpt, d);
        dpt = cpt;
    }
    reg_[0x052 / 2] = uint16_t(err);
    setPtr(0x048, cpt); setPtr(0x054, dpt);
    reg_[0x040 / 2] = uint16_t((con0 & 0x0FFF) | (shift << 12));
}

} // namespace amiga
