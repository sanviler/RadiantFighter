#pragma once
// A small options menu drawn over the game picture with a built-in 5x7 font (its own, upper case,
// digits and common signs). Generic: the items and their values come from the program; nothing here depends
// on the game, so the panel can be reused by other ports.
#include "formats/palette.h"
#include <string>
#include <vector>

namespace dm {

struct MenuItem {
    std::string label;
    std::string value;          // empty: an action ("QUIT", "BACK")
};

class OptionsPanel {
public:
    bool open = false;
    int focus = 0;
    std::string title = "OPTIONS";
    std::string footer;         // a line at the bottom (for example the name of a joystick)
    // draws the panel centred in an RGB picture of w x h pixels; the picture behind is darkened
    void draw(std::vector<dm::Rgb>& px, int w, int h, const std::vector<MenuItem>& items) const;
    void move(int dir, int count) { focus = (focus + dir + count) % count; }

    // text with the built-in font (6 pixels per character, 8 per line); unknown characters are blank;
    // char codes 1-8 are arrows: right, left, up, down, down-right, down-left, up-left, up-right
    static void text(std::vector<dm::Rgb>& px, int w, int h, int x, int y, const std::string& s, dm::Rgb c);
    static void box(std::vector<dm::Rgb>& px, int w, int h, int x, int y, int bw, int bh, dm::Rgb c);
    static int textWidth(const std::string& s) { return int(s.size()) * 6; }
};

} // namespace dm
