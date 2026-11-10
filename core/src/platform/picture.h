#pragma once
// Picture filters of the video layer, without SDL (used by dm::Video and by the off-screen simulator).
//
// Two ways to put a w x h picture on a W x H output (W, H = the area the picture covers, aspect kept):
// - integer scale s (W = w*s, H = h*s): sharp pixels, or the CRT filter (crt-pixie style) at exactly s;
// - fill (any scale): "sharp bilinear" (every source pixel a flat block, a one-output-pixel soft edge between
//   blocks: no uneven pixels, no shimmer when scrolling), or the CRT filter for non-integer scales: every output
//   row is the average of the gaussian beams over the height of that row (area sampling), so all scanlines
//   carry the same light even when a source row covers 5.6 output rows; the aperture grille has a fixed period
//   of 3 output columns, like the phosphors of a real screen.
#include "formats/palette.h"
#include <cstdint>
#include <vector>

namespace dm::picture {

struct Work {                                  // buffers kept between frames
    std::vector<float> lin, glow, rows, tmp;
    std::vector<float> fillE;                  // fill CRT: beam weights per output row, 4 rows x levels
    std::vector<int> fillR0;                   // fill CRT: first source row per output row
    int fillH = 0, fillh = 0;
};

// out: W*H*3 bytes (RGB24). `src` is w*h pixels.
void integerSharp(const Rgb* src, int w, int h, int s, uint8_t* out);
void integerCrt(const Rgb* src, int w, int h, int s, uint8_t* out, Work& wk);
void fillSharp(const Rgb* src, int w, int h, int W, int H, uint8_t* out);
void fillCrt(const Rgb* src, int w, int h, int W, int H, uint8_t* out, Work& wk);

// size of the picture on an ow x oh output: integer = largest integer scale (at least 1); fill = largest
// size with the aspect of w x h (each side rounded to the nearest pixel)
void integerSize(int w, int h, int ow, int oh, int& W, int& H, int& s);
void fillSize(int w, int h, int ow, int oh, int& W, int& H);

} // namespace dm::picture
