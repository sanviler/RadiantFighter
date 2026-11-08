#include "game/fixes.h"
#include <cstring>
#include <string>

namespace dm::game {

namespace {
// replaces a text with one of the same length, only if the expected text is at the address
bool replaceText(amiga::Machine& m, uint32_t off, const char* expect, const char* with) {
    const size_t n = std::strlen(expect);
    if (std::strlen(with) != n) return false;
    const uint32_t a = m.gameBase() + off;
    for (size_t i = 0; i < n; ++i) if (m.peek8(a + uint32_t(i)) != uint8_t(expect[i])) {
        if (m.log) m.log(std::string("fix not applied (different text): ") + with);
        return false;
    }
    for (size_t i = 0; i < n; ++i) m.poke8(a + uint32_t(i), uint8_t(with[i]));
    return true;
}
} // namespace

void applyFixes(amiga::Machine& m) {
    // level start screen, Italian text: typo in the original (fixed 40-character field, stays centred)
    replaceText(m, 0x183A1, "PREMI FUOLO  PER COMINCIARE ", " PREMI FUOCO PER COMINCIARE ");
}

} // namespace dm::game
