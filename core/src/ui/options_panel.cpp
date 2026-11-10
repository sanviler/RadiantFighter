#include "ui/options_panel.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace dm {

namespace {
// 5x7 glyphs, one string of 35 characters per glyph ('#' = pixel), rows from the top
struct Glyph { char c; const char* rows; };
constexpr Glyph kFont[] = {
    {'A', ".###.#...##...#######...##...##...#"}, {'B', "####.#...#####.#...##...##...#####."},
    {'C', ".###.#...##....#....#....#...#.###."}, {'D', "####.#...##...##...##...##...#####."},
    {'E', "######....####.#....#....#....#####"}, {'F', "######....####.#....#....#....#...."},
    {'G', ".###.#...##....#.####...##...#.####"}, {'H', "#...##...#######...##...##...##...#"},
    {'I', ".###...#....#....#....#....#...###."}, {'J', "..###...#....#....#....#.#..#..##.."},
    {'K', "#...##..#.#.#..##...#.#..#..#.#...#"}, {'L', "#....#....#....#....#....#....#####"},
    {'M', "#...###.###.#.##...##...##...##...#"}, {'N', "#...###..##.#.##..###...##...##...#"},
    {'O', ".###.#...##...##...##...##...#.###."}, {'P', "####.#...##...#####.#....#....#...."},
    {'Q', ".###.#...##...##...##.#.##..#..##.#"}, {'R', "####.#...##...#####.#.#..#..#.#...#"},
    {'S', ".####.....#.....###.....#....#####."}, {'T', "#####..#....#....#....#....#....#.."},
    {'U', "#...##...##...##...##...##...#.###."}, {'V', "#...##...##...##...#.#.#..#.#...#.."},
    {'W', "#...##...##...##.#.##.#.###.###...#"}, {'X', "#...##...#.#.#...#...#.#.#...##...#"},
    {'Y', "#...##...#.#.#...#....#....#....#.."}, {'Z', "#####....#...#...#...#...#....#####"},
    {'0', ".###.#...##..###.#.###..##...#.###."}, {'1', "..#...##....#....#....#....#...###."},
    {'2', ".###.#...#....#...#...#...#...#####"}, {'3', "#####...#...#.....#.....##...#.###."},
    {'4', "...#...##..#.#.#..#.#####...#....#."}, {'5', "######....####.....#....##...#.###."},
    {'6', "..##..#...#....####.#...##...#.###."}, {'7', "#####....#...#...#...#....#....#..."},
    {'8', ".###.#...##...#.###.#...##...#.###."}, {'9', ".###.#...##...#.####....#...#..##.."},
    {'.', "..........................##...##.."}, {',', ".........................##..#..#.."},
    {':', "......##...##........##...##......."}, {'-', "...............#####..............."},
    {'/', ".........#...#...#...#...#........."}, {'(', "...#...#...#....#....#.....#.....#."},
    {')', ".#.....#.....#....#....#...#...#..."}, {'!', "..#....#....#....#....#.........#.."},
    {'?', ".###.#...#....#...#...#.........#.."}, {'\'', "..#....#...#......................."},
    {'+', ".......#....#..#####..#....#......."}, {'=', "..........#####.....#####.........."},
    {'<', "...#...#...#...#.....#.....#.....#."}, {'>', ".#.....#.....#.....#...#...#...#..."},
    {'#', ".#.#..#.#.#####.#.#.#####.#.#..#.#."}, {'%', "##...##..#...#...#...#...#..##...##"},
    {'_', "..............................#####"}, {'*', ".......#..#.#.#.###.#.#.#..#......."},
    // arrows (char codes 1-8): right, left, up, down, down-right, down-left, up-left, up-right
    {'\x01', ".......#.....#.#####...#...#......."}, {'\x02', ".......#...#...#####.#.....#......."}, {'\x03', "..#...###.#.#.#..#....#....#....#.."}, {'\x04', "..#....#....#....#..#.#.#.###...#.."}, {'\x05', "#.....#.....#.#...##..###.........."}, {'\x06', "....#...#.#.#..##...###............"}, {'\x07', "###..##...#.#.....#.....#.........."}, {'\x08', "..###...##..#.#.#...#.............."},
};

const char* glyph(char c) {
    c = char(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& g : kFont) if (g.c == c) return g.rows;
    return nullptr;
}

void fill(std::vector<dm::Rgb>& px, int w, int h, int x0, int y0, int bw, int bh, dm::Rgb c) {
    for (int y = std::max(0, y0); y < std::min(h, y0 + bh); ++y)
        for (int x = std::max(0, x0); x < std::min(w, x0 + bw); ++x) px[size_t(y) * w + x] = c;
}
} // namespace

void OptionsPanel::text(std::vector<dm::Rgb>& px, int w, int h, int x, int y, const std::string& s, dm::Rgb c) {
    for (char ch : s) {
        if (const char* g = glyph(ch); g && std::strlen(g) >= 35) {
            for (int r = 0; r < 7; ++r)
                for (int k = 0; k < 5; ++k)
                    if (g[r * 5 + k] == '#') {
                        const int xx = x + k, yy = y + r;
                        if (xx >= 0 && yy >= 0 && xx < w && yy < h) px[size_t(yy) * w + xx] = c;
                    }
        }
        x += 6;
    }
}

void OptionsPanel::box(std::vector<dm::Rgb>& px, int w, int h, int x, int y, int bw, int bh, dm::Rgb c) { fill(px, w, h, x, y, bw, bh, c); }

void OptionsPanel::darken(std::vector<dm::Rgb>& px) {
    for (auto& p : px) { p.r = uint8_t(p.r / 3); p.g = uint8_t(p.g / 3); p.b = uint8_t(p.b / 3); }
}

std::vector<std::string> OptionsPanel::wrap(const std::string& s, int maxW) {
    const size_t n = size_t(std::max(1, maxW / 6));
    std::vector<std::string> out;
    std::string rest = s;
    while (rest.size() > n) {
        size_t cut = rest.rfind(' ', n);
        if (cut == std::string::npos || cut == 0) cut = n;          // no space: split the word
        out.push_back(rest.substr(0, cut));
        rest = rest.substr(cut);
        while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    }
    if (!rest.empty() || out.empty()) out.push_back(rest);
    return out;
}

int OptionsPanel::scaleFor(int ow, int oh, int w, int h) {
    int s = std::max(1, std::min(ow * 7 / 10 / kWidth, oh * 7 / 10 / kHeight));
    while (s > 1 && (w * s > ow || h * s > oh)) --s;
    return s;
}

void OptionsPanel::render(const std::vector<MenuItem>& items) {
    constexpr int kRow = 11, kLine = 9, kIn = kWidth - 24;      // item row, wrapped line, text width inside
    const dm::Rgb border{200, 120, 40}, back{16, 12, 24}, normal{200, 200, 210}, hi{255, 220, 90}, dim{140, 140, 150};
    // layout of the items: label and value on one row when they fit, otherwise the label (split if needed)
    // and below it the value aligned to the right (split if needed)
    struct Line { std::string s; bool right; };
    std::vector<std::vector<Line>> rows(items.size());
    auto oneRow = [&](const std::vector<Line>& r) {
        return r.size() == 2 && r[1].right && textWidth(r[0].s) + 12 + textWidth(r[1].s) <= kIn;
    };
    auto lineCount = [&](const std::vector<Line>& r) { return oneRow(r) ? 1 : std::max(1, int(r.size())); };
    int y = 24;
    for (size_t i = 0; i < items.size(); ++i) {
        const std::string v = items[i].value.empty() ? "" : (int(i) == focus ? "< " + items[i].value + " >" : items[i].value);
        if (textWidth(items[i].label) + (v.empty() ? 0 : 12 + textWidth(v)) <= kIn) {
            rows[i].push_back({items[i].label, false});
            if (!v.empty()) rows[i].push_back({v, true});           // same row
        } else {
            for (auto& l : wrap(items[i].label, kIn)) rows[i].push_back({l, false});
            for (auto& l : wrap(v, kIn)) rows[i].push_back({l, true});
        }
        y += kRow + (lineCount(rows[i]) - 1) * kLine;
    }
    const std::vector<std::string> foot = footer.empty() ? std::vector<std::string>{} : wrap(footer, kIn);
    const int footLines = std::max(kFooterLines, int(foot.size()));
    h_ = std::max(kHeight, y + 6 + footLines * kLine + 6);
    px_.assign(size_t(kWidth) * size_t(h_), back);
    const int W = kWidth, H = h_;
    fill(px_, W, H, 0, 0, W, 2, border); fill(px_, W, H, 0, H - 2, W, 2, border);
    fill(px_, W, H, 0, 0, 2, H, border); fill(px_, W, H, W - 2, 0, 2, H, border);
    text(px_, W, H, (W - textWidth(title)) / 2, 8, title, hi);
    y = 24;
    for (size_t i = 0; i < items.size(); ++i) {
        const bool f = int(i) == focus;
        const auto& r = rows[i];
        const bool one = oneRow(r);
        const int lines = lineCount(r);
        if (f) fill(px_, W, H, 6, y - 2, W - 12, kRow + (lines - 1) * kLine, dm::Rgb{60, 40, 70});
        int ly = y;
        for (size_t k = 0; k < r.size(); ++k) {
            const int x = r[k].right ? W - 12 - textWidth(r[k].s) : 12;
            text(px_, W, H, x, ly, r[k].s, f ? hi : normal);
            if (!(one && k == 0)) ly += kLine;
        }
        y += kRow + (lines - 1) * kLine;
    }
    // footer: at the bottom, centred lines
    int fy = H - 8 - int(foot.size()) * kLine + 1;
    for (const auto& l : foot) { text(px_, W, H, (W - textWidth(l)) / 2, fy, l, dim); fy += kLine; }
}

} // namespace dm
