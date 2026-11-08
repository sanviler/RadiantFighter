#pragma once
// RadiantFighter: special moves of each fighter, shown in the options menu (MOVE LIST).
// Based on the move list "All special moves for Shadow Fighter" by Dracs (1994), included as pictures
// (Moves1-5) in the WHDLoad install of the game.
// Directions: \x01 right, \x02 left, \x03 up, \x04 down, \x05 down-right, \x06 down-left, \x07 up-left,
// \x08 up-right (arrow glyphs of the options panel font). "RAPID FIRE" = press fire repeatedly.
#include <vector>

namespace rf {

struct Move { const char* name; const char* input; };
struct FighterMoves { const char* fighter; std::vector<Move> moves; };

inline const std::vector<FighterMoves>& moveList() {
    static const std::vector<FighterMoves> list = {
        {"SLAMDUNK", {{"JUMPING B BALL", "\x05\x01 FIRE"}, {"SPINNING FIRE KICK", "\x04\x03 FIRE"},
                      {"SPINNING FIRE B BALL", "\x06\x02 FIRE"}}},
        {"ELECTRA", {{"ELECTRIC BODY", "RAPID FIRE"}, {"POWER LAUNCH", "\x02\x07 FIRE"},
                     {"ELECTRIC BOOMERANG", "\x06\x02 FIRE"}, {"DOUBLE KICK", "\x04\x03 FIRE"},
                     {"SPINNING JUMP", "\x05\x01 FIRE"}}},
        {"SORIA", {{"SPINNING ROUND HOUSE ATTACK", "\x06\x02 FIRE"}, {"DOUBLE DROP HEEL KICK", "\x02\x07 FIRE"}}},
        {"YURGEN", {{"GUN FIRE", "\x06\x02 FIRE"}, {"POWER FIST", "\x05\x01 FIRE"}}},
        {"CODY", {{"KUTO KICK", "\x01\x05 FIRE"}, {"FAST PUNCH", "RAPID FIRE"}, {"KUTO FIREBALL", "\x06\x02 FIRE"}}},
        {"FAKIR", {{"GENIE HURRICANE", "\x04\x06 FIRE"}, {"MYSTERY FIRE", "\x04\x05 FIRE"},
                   {"MAGIC CARPET", "\x05\x04 FIRE"}}},
        {"SALVADOR", {{"TURNING FLIP KICK", "\x05\x01 FIRE"}, {"FLYING KICK", "\x04\x03 FIRE"}}},
        {"KURY", {{"POWER SMASH FIST", "\x04\x03 FIRE"}, {"BODY DROP", "\x04\x05 FIRE"},
                  {"SPINNING FIRE HAND", "\x06\x04 FIRE"}}},
        {"TONI", {{"BURNING UPPERCUT", "\x04\x03 FIRE"}, {"FLAME KICK", "\x06\x04 FIRE"},
                  {"MASSIVE UPPERCUT", "\x01\x05 FIRE"}}},
        {"LEE CHEN", {{"FIERY HANDSPRING", "\x04\x03 FIRE"}, {"FIST OF FALLING SUN", "RAPID FIRE"},
                      {"FIRE FIST", "\x04\x07 FIRE"}, {"FURY SPIN", "\x04\x06 FIRE"},
                      {"FALLING NUNCHAKU", "\x04\x05 FIRE"}}},
        {"MANX", {{"FIRE HANDS", "\x04\x05 FIRE"}, {"CUTTING CLAWS", "\x01\x05 FIRE"},
                  {"TIGER POUNCE ATTACK", "\x05\x01 FIRE"}, {"FALLING ANGEL", "\x06\x02 FIRE"}}},
        {"TOP KNOT", {{"FAST FIRE", "\x01\x05 FIRE"}, {"SPINNING KICK", "\x04\x06 FIRE"},
                      {"SPEED ELBOW SMASH", "\x04\x05 FIRE"}, {"UPPER CUT", "\x04\x03 FIRE"}}},
        {"YARADO", {{"ELECTRIC SPEED", "\x01\x05\x04\x06\x02 FIRE"}, {"SPIRIT POWER", "\x06\x02 FIRE"}}},
        {"OKURA", {{"ELECTRIC SWORD", "RAPID FIRE"}, {"SPINNING BLADE", "\x04\x05 FIRE"},
                   {"THUNDER POWER", "\x01\x05\x04\x06\x02 FIRE"}}},
        {"TOSHIO", {{"FIRE BALL", "\x04\x06 FIRE"}, {"SPEED DRAGON", "\x01\x05\x04\x06\x02 FIRE"},
                    {"DRAGON UPPERCUT", "\x04\x03 FIRE"}, {"DRAGON KICK", "\x03\x07 FIRE"}}},
        {"KRHOME", {{"LIQUID SILVER ATTACK", "\x01\x05\x04\x06\x02 FIRE"}, {"MELTING BODY", "\x02\x06\x04\x05\x01 FIRE"}}},
    };
    return list;
}

} // namespace rf
