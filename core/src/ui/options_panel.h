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

// The panel has the same fixed size in every port (kWidth x kHeight pixels of its own, independent of the game
// resolution) and is drawn by the video layer over the filtered picture (never through the CRT filter), at an
// integer scale of its own. Long labels, values and footers are split on more lines; only if the text needs more
// rows than the panel has, the panel grows downwards (the width never changes).
class OptionsPanel {
public:
    static constexpr int kWidth = 256, kHeight = 200;
    static constexpr int kFooterLines = 3;          // rows kept free at the bottom for the footer
    bool open = false;
    int focus = 0;
    std::string title = "OPTIONS";
    std::string footer;         // a line at the bottom (for example the name of a joystick); split if long
    // draws the panel into its own picture: width() x height() pixels
    void render(const std::vector<MenuItem>& items);
    const std::vector<dm::Rgb>& pixels() const { return px_; }
    int width() const { return kWidth; }
    int height() const { return h_; }
    void move(int dir, int count) { focus = (focus + dir + count) % count; }

    // the game picture behind an open panel: darker
    static void darken(std::vector<dm::Rgb>& px);
    // splits a string into lines of at most maxW pixels (at the spaces when possible)
    static std::vector<std::string> wrap(const std::string& s, int maxW);
    // text with the built-in font (6 pixels per character, 8 per line); unknown characters are blank;
    // char codes 1-8 are arrows: right, left, up, down, down-right, down-left, up-left, up-right
    static void text(std::vector<dm::Rgb>& px, int w, int h, int x, int y, const std::string& s, dm::Rgb c);
    static void box(std::vector<dm::Rgb>& px, int w, int h, int x, int y, int bw, int bh, dm::Rgb c);
    static int textWidth(const std::string& s) { return int(s.size()) * 6; }
    // integer scale of the panel pixels on an ow x oh output: the same in every port (about 70% of the output
    // for a kWidth x kHeight panel), smaller only if a w x h picture would not fit
    static int scaleFor(int ow, int oh, int w = kWidth, int h = kHeight);

private:
    std::vector<dm::Rgb> px_;
    int h_ = kHeight;
};

} // namespace dm
