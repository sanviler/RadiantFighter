#pragma once
// Bits of Machine::setCustom1 for RadiantFighter.
#include <cstdint>

namespace rf {

constexpr uint32_t kSkipIntro = 1u << 0;   // straight to the title screen

bool introRunning();                       // the intro is running (Space / Return / fire skip it)

} // namespace rf
