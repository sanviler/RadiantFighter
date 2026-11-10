// Off-screen check of the picture modes: coverage and scanline regularity on simulated screens.
#include "platform/picture.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
using namespace dm;
static float lin(uint8_t v) { return std::pow(v / 255.0f, 2.2f); }
// regularity of the scanlines on a flat grey field: light per source row (area of the vertical profile over
// the output rows of that row) and peak per row, as max deviation from the neighbours (vignette removed)
static void metric(const char* name, const std::vector<uint8_t>& o, int W, int H, int h) {
    std::vector<double> P(static_cast<size_t>(H));
    for (int Y = 0; Y < H; ++Y) { double a = 0; int n = 0; for (int X = W / 3; X < 2 * W / 3; ++X) { a += lin(o[(size_t(Y) * W + X) * 3 + 1]); ++n; } P[size_t(Y)] = a / n; }
    const double sy = double(H) / h;
    std::vector<double> E(size_t(h), 0), M(size_t(h), 0);
    for (int Y = 0; Y < H; ++Y) {
        const double y0 = Y / sy, y1 = (Y + 1) / sy;
        for (int r = int(y0); r <= int(y1) && r < h; ++r) {
            const double ov = std::max(0.0, std::min(y1, r + 1.0) - std::max(y0, double(r)));
            E[size_t(r)] += P[size_t(Y)] * ov * sy;
            M[size_t(r)] = std::max(M[size_t(r)], P[size_t(Y)]);
        }
    }
    double de = 0, dm = 0;
    for (int r = h / 4; r < 3 * h / 4; ++r) {
        de = std::max(de, std::fabs(E[size_t(r)] / ((E[size_t(r - 1)] + E[size_t(r + 1)]) / 2) - 1));
        dm = std::max(dm, std::fabs(M[size_t(r)] / ((M[size_t(r - 1)] + M[size_t(r + 1)]) / 2) - 1));
    }
    std::printf("  %-10s light/row dev %5.2f%%  peak/row dev %5.2f%%\n", name, de * 100, dm * 100);
}
int main(int argc, char** argv) {
    const int w = argc > 2 ? std::atoi(argv[2]) : 320, h = argc > 1 ? std::atoi(argv[1]) : 256;   // picture_check [height [width]]
    std::vector<Rgb> flat(size_t(w) * h, Rgb{180, 180, 180});
    const int screens[][2] = {{1920, 1080}, {2560, 1440}, {3840, 2160}, {1280, 720}};
    picture::Work wk;
    for (auto& s : screens) {
        int Wi, Hi, k, Wf, Hf;
        picture::integerSize(w, h, s[0], s[1], Wi, Hi, k);
        picture::fillSize(w, h, s[0], s[1], Wf, Hf);
        std::printf("%dx%d, picture %dx%d: integer x%d = %dx%d (height %.0f%%, area %.0f%%); fill x%.2f = %dx%d (height %.0f%%)\n",
            s[0], s[1], w, h, k, Wi, Hi, 100.0 * Hi / s[1], 100.0 * Wi * Hi / (s[0] * s[1]), double(Hf) / h, Wf, Hf, 100.0 * Hf / s[1]);
        std::vector<uint8_t> o;
        o.resize(size_t(Wi) * Hi * 3); picture::integerCrt(flat.data(), w, h, k, o.data(), wk); metric("int+crt", o, Wi, Hi, h);
        o.resize(size_t(Wf) * Hf * 3); picture::fillCrt(flat.data(), w, h, Wf, Hf, o.data(), wk); metric("fill+crt", o, Wf, Hf, h);
        // the old way: CRT at the integer scale, then stretched (nearest) to the fill size
        std::vector<uint8_t> a(size_t(Wi) * Hi * 3); picture::integerCrt(flat.data(), w, h, k, a.data(), wk);
        for (int Y = 0; Y < Hf; ++Y) for (int X = 0; X < Wf; ++X) for (int c = 0; c < 3; ++c)
            o[(size_t(Y) * Wf + X) * 3 + c] = a[(size_t(Y * Hi / Hf) * Wi + X * Wi / Wf) * 3 + c];
        metric("stretched", o, Wf, Hf, h);
    }
}
