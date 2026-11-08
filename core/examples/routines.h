#pragma once
// Game routines translated to C++ and hooked into the original code.
// Every routine can be used in two ways:
//   Replace: the C++ function replaces the 68000 routine (the game skips the original code)
//   Verify:  the result is computed in C++, then the original code runs and registers and memory
//            are compared; differences go to the log and the counter.
// Addresses are offsets from the start of MAIN.BIN.
#include "amiga/machine.h"
#include <cstdint>
#include <string>

namespace dm::game {

enum class HookMode { Off, Replace, Verify };

struct RoutineStats {
    uint64_t calls = 0, mismatches = 0;
};

struct Routines {
    RoutineStats largestFreeBlock;   // $0A3EE: largest free block of a memory pool
    RoutineStats mirrorGraphic;      // $0AA92: mirrored copy of a graphic (5 planes, table $18E6E)
};

// to be called once MAIN.BIN is loaded (onGamePatched)
void installRoutines(amiga::Machine& m, HookMode mode, Routines& stats);
std::string describe(const Routines& r);

} // namespace dm::game
