#include "platform/picture.h"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace dm::picture {

namespace {

// runs f(begin, end) on slices of [0, n) in parallel (the CRT filter is too slow on one core at 4x and above).
// The worker threads are made once and wait between frames (making threads every frame costs milliseconds).
class Pool {
public:
    Pool() : n_(std::clamp(int(std::thread::hardware_concurrency()), 1, 8)) {
        for (int i = 1; i < n_; ++i) th_.emplace_back([this, i] { loop(i); });
    }
    ~Pool() {
        { std::lock_guard<std::mutex> l(m_); quit_ = true; ++gen_; }
        cv_.notify_all();
        for (auto& t : th_) t.join();
    }
    int size() const { return n_; }
    void run(const std::function<void(int)>& job) {
        std::unique_lock<std::mutex> l(m_);
        job_ = &job; left_ = n_ - 1; ++gen_;
        l.unlock();
        cv_.notify_all();
        job(0);
        l.lock();
        done_.wait(l, [this] { return left_ == 0; });
        job_ = nullptr;
    }
private:
    void loop(int i) {
        unsigned seen = 0;
        for (;;) {
            std::unique_lock<std::mutex> l(m_);
            cv_.wait(l, [&] { return gen_ != seen; });
            seen = gen_;
            if (quit_) return;
            const std::function<void(int)>* j = job_;
            l.unlock();
            if (j) (*j)(i);
            l.lock();
            if (--left_ == 0) done_.notify_one();
        }
    }
    int n_;
    std::vector<std::thread> th_;
    std::mutex m_;
    std::condition_variable cv_, done_;
    const std::function<void(int)>* job_ = nullptr;
    unsigned gen_ = 0;
    int left_ = 0;
    bool quit_ = false;
};
Pool& pool() { static Pool p; return p; }

template <class F> void parallelRows(int n, F f) {
    Pool& p = pool();
    const int t = p.size();
    if (t == 1 || n < 64) { f(0, n); return; }
    const std::function<void(int)> job = [&](int i) { f(n * i / t, n * (i + 1) / t); };
    p.run(job);
}

struct Lut {
    float toLin[256];
    uint8_t toGam[4097];
    Lut() {
        for (int i = 0; i < 256; ++i) toLin[i] = std::pow(i / 255.0f, 2.2f);
        for (int i = 0; i <= 4096; ++i) toGam[i] = uint8_t(std::lround(std::pow(i / 4096.0f, 1 / 2.2f) * 255));
    }
};
const Lut& lut() { static const Lut l; return l; }

constexpr int kLev = 64;                       // brightness levels of the beam width
constexpr float kGain = 1.75f;                 // beam peak (keeps the average brightness of the picture)
constexpr float kFillWiden = 1.5f;          // softer beams at non-integer scales (less difference between scanlines)
inline float beamWidth(int l) { return 0.18f + 0.14f * std::sqrt(l / float(kLev)); }   // in source rows

// linear picture, glow (5x3 box blur) and every source row resampled to the output width (smoothstep between
// pixel centres: sharp centres, soft edges); sx = output columns per source column
void prepare(const Rgb* src, int w, int h, int W, float sx, Work& wk, bool resample = true) {
    const Lut& L = lut();
    const size_t n = size_t(w) * h;
    wk.lin.resize(n * 3); wk.glow.resize(n * 3);
    wk.tmp.resize(n * 3);
    // linear picture and the horizontal 5-pixel sums of the glow, then the vertical 3-row sums (separable box)
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const Rgb* s = src + size_t(y) * w;
            float* l = &wk.lin[size_t(y) * w * 3];
            for (int x = 0; x < w; ++x) { l[x * 3] = L.toLin[s[x].r]; l[x * 3 + 1] = L.toLin[s[x].g]; l[x * 3 + 2] = L.toLin[s[x].b]; }
            float* t = &wk.tmp[size_t(y) * w * 3];
            for (int x = 0; x < w; ++x)
                for (int c = 0; c < 3; ++c) {
                    float a = 0;
                    for (int xx = std::max(0, x - 2); xx <= std::min(w - 1, x + 2); ++xx) a += l[xx * 3 + c];
                    t[x * 3 + c] = a;
                }
        }
    });
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const int ya = std::max(0, y - 1), yb = std::min(h - 1, y + 1);
            float* g = &wk.glow[size_t(y) * w * 3];
            for (int x = 0; x < w; ++x) {
                const int k = (std::min(w - 1, x + 2) - std::max(0, x - 2) + 1) * (yb - ya + 1);
                for (int c = 0; c < 3; ++c) {
                    float a = 0;
                    for (int yy = ya; yy <= yb; ++yy) a += wk.tmp[(size_t(yy) * w + x) * 3 + c];
                    g[x * 3 + c] = a / float(k);
                }
            }
        }
    });
    if (!resample) return;
    wk.rows.resize(size_t(h) * W * 3);
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* s = &wk.lin[size_t(y) * w * 3];
            float* d = &wk.rows[size_t(y) * W * 3];
            for (int X = 0; X < W; ++X) {
                const float u = (X + 0.5f) / sx - 0.5f;
                const int x0 = int(std::floor(u)); float t = u - float(x0);
                t = t * t * (3 - 2 * t);
                const int xa = std::clamp(x0, 0, w - 1), xb = std::clamp(x0 + 1, 0, w - 1);
                for (int c = 0; c < 3; ++c) d[X * 3 + c] = s[xa * 3 + c] * (1 - t) + s[xb * 3 + c] * t;
            }
        }
    });
}

} // namespace

void integerSize(int w, int h, int ow, int oh, int& W, int& H, int& s) {
    s = std::max(1, std::min(ow / std::max(w, 1), oh / std::max(h, 1)));
    W = w * s; H = h * s;
}

void fillSize(int w, int h, int ow, int oh, int& W, int& H) {
    if (int64_t(ow) * h >= int64_t(oh) * w) { H = oh; W = int(std::lround(double(oh) * w / h)); }
    else { W = ow; H = int(std::lround(double(ow) * h / w)); }
    W = std::clamp(W, 1, std::max(ow, 1)); H = std::clamp(H, 1, std::max(oh, 1));
}

void integerSharp(const Rgb* src, int w, int h, int s, uint8_t* out) {
    const int W = w * s;
    for (int Y = 0; Y < h * s; ++Y)
        for (int X = 0; X < W; ++X) {
            const Rgb c = src[size_t(Y / s) * w + X / s];
            uint8_t* p = out + (size_t(Y) * W + X) * 3;
            p[0] = c.r; p[1] = c.g; p[2] = c.b;
        }
}

// CRT at an integer scale: every output row mixes the two nearest source rows with a gaussian beam whose width
// grows with the brightness; aperture grille (R, G, B columns), a little glow and a soft vignette.
void integerCrt(const Rgb* src, int w, int h, int s, uint8_t* out, Work& wk) {
    const Lut& L = lut();
    const int W = w * s, H = h * s;
    prepare(src, w, h, W, float(s), wk);
    const float cx = W * 0.5f, cy = H * 0.5f;
    std::vector<float> vigX(static_cast<size_t>(W)), maskW(size_t(W) * 3);
    for (int X = 0; X < W; ++X) {
        const float vx = (X - cx) / cx; vigX[size_t(X)] = vx * vx;
        const int sub = (X * 3 / s) % 3;
        for (int c = 0; c < 3; ++c) maskW[size_t(X) * 3 + c] = (sub == c) ? 1.0f : 0.72f;
    }
    parallelRows(H, [&](int Y0, int Y1) {
        float ea[kLev + 1], eb[kLev + 1];
        for (int Y = Y0; Y < Y1; ++Y) {
            const float v = (Y + 0.5f) / float(s) - 0.5f;
            const int r0 = int(std::floor(v)); const float d = v - float(r0);
            for (int l = 0; l <= kLev; ++l) {
                const float bw = beamWidth(l);
                ea[l] = std::exp(-(d * d) / (2 * bw * bw)) * kGain;
                eb[l] = std::exp(-((1 - d) * (1 - d)) / (2 * bw * bw)) * kGain;
            }
            const int ra = std::clamp(r0, 0, h - 1), rb = std::clamp(r0 + 1, 0, h - 1);
            const float* A = &wk.rows[size_t(ra) * W * 3];
            const float* B = &wk.rows[size_t(rb) * W * 3];
            const int gy = std::clamp(int((Y + 0.5f) / float(s)), 0, h - 1);
            const float vy = (Y - cy) / cy, vy2 = vy * vy;
            const float* G = &wk.glow[size_t(gy) * w * 3];
            uint8_t* o = out + size_t(Y) * W * 3;
            for (int X = 0; X < W; ++X) {
                const float vig = 1.0f - 0.12f * (vigX[size_t(X)] + vy2);
                const float* g = &G[size_t(std::min(X / s, w - 1)) * 3];
                for (int c = 0; c < 3; ++c) {
                    const float a = A[X * 3 + c], b = B[X * 3 + c];
                    float val = a * ea[int(std::min(a, 1.0f) * kLev)] + b * eb[int(std::min(b, 1.0f) * kLev)];
                    val = (val * maskW[size_t(X) * 3 + c] + 0.10f * g[c]) * vig;
                    o[X * 3 + c] = L.toGam[std::clamp(int(val * 4096), 0, 4096)];
                }
            }
        }
    });
}

void fillSharp(const Rgb* src, int w, int h, int W, int H, uint8_t* out) {
    const float sx = float(W) / w, sy = float(H) / h;
    // per output column / row: the two source pixels and the weight of the second (sharp bilinear)
    auto axis = [](int n, int N, float sc, std::vector<int>& i0, std::vector<float>& t) {
        i0.resize(size_t(N)); t.resize(size_t(N));
        for (int X = 0; X < N; ++X) {
            const float u = (X + 0.5f) / sc - 0.5f;
            const int x0 = int(std::floor(u));
            const float f = std::clamp((u - float(x0) - 0.5f) * sc + 0.5f, 0.0f, 1.0f);
            i0[size_t(X)] = x0; t[size_t(X)] = f;
            (void)n;
        }
    };
    std::vector<int> xi, yi; std::vector<float> xt, yt;
    axis(w, W, sx, xi, xt); axis(h, H, sy, yi, yt);
    parallelRows(H, [&](int Y0, int Y1) {
        for (int Y = Y0; Y < Y1; ++Y) {
            const int ya = std::clamp(yi[size_t(Y)], 0, h - 1), yb = std::clamp(yi[size_t(Y)] + 1, 0, h - 1);
            const float ty = yt[size_t(Y)];
            const Rgb* ra = src + size_t(ya) * w; const Rgb* rb = src + size_t(yb) * w;
            uint8_t* o = out + size_t(Y) * W * 3;
            for (int X = 0; X < W; ++X) {
                const int xa = std::clamp(xi[size_t(X)], 0, w - 1), xb = std::clamp(xi[size_t(X)] + 1, 0, w - 1);
                const float tx = xt[size_t(X)];
                const Rgb p00 = ra[xa], p01 = ra[xb], p10 = rb[xa], p11 = rb[xb];
                auto mix = [&](uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
                    const float top = a + (b - a) * tx, bot = c + (d - c) * tx;
                    return uint8_t(top + (bot - top) * ty + 0.5f);
                };
                o[X * 3] = mix(p00.r, p01.r, p10.r, p11.r);
                o[X * 3 + 1] = mix(p00.g, p01.g, p10.g, p11.g);
                o[X * 3 + 2] = mix(p00.b, p01.b, p10.b, p11.b);
            }
        }
    });
}

// CRT at any scale: each output row is the average, over its own height, of the beams of the source rows that
// reach it (closed form with erf), so the light of every scanline is the same wherever it falls on the output
// grid; the grille has a fixed period of 3 output columns.
// For speed the beams are computed on the source columns (w per output row, not W) and only then widened to the
// output width with the same smoothstep as the integer filter (sharp centres, soft edges).
void fillCrt(const Rgb* src, int w, int h, int W, int H, uint8_t* out, Work& wk) {
    const Lut& L = lut();
    const float sx = float(W) / w, sy = float(H) / h;
    prepare(src, w, h, W, sx, wk, false);
    // beam weights per output row: up to 4 source rows, for every brightness level (kept between frames in
    // `wk`; rows outside the picture weigh 0)
    constexpr int kE = 4 * (kLev + 1);
    if (wk.fillH != H || wk.fillh != h || wk.fillR0.size() != size_t(H)) {
        wk.fillR0.assign(size_t(H), 0);
        wk.fillE.assign(size_t(H) * kE, 0.0f);
        for (int Y = 0; Y < H; ++Y) {
            const float v0 = Y / sy, v1 = (Y + 1) / sy;          // source rows covered by this output row
            const int r0 = int(std::floor((v0 + v1) * 0.5f)) - 1;
            wk.fillR0[size_t(Y)] = r0;
            for (int k = 0; k < 4; ++k) {
                if (r0 + k < 0 || r0 + k >= h) continue;
                const float rc = float(r0 + k) + 0.5f;            // centre of the beam of that source row
                for (int l = 0; l <= kLev; ++l) {
                    const float bw = beamWidth(l) * kFillWiden, q = 1.0f / (bw * std::sqrt(2.0f));
                    const float area = bw * std::sqrt(3.14159265f / 2) * (std::erf((v1 - rc) * q) - std::erf((v0 - rc) * q));
                    wk.fillE[size_t(Y) * kE + size_t(k) * (kLev + 1) + size_t(l)] = kGain / kFillWiden * area / (v1 - v0);
                }
            }
        }
        wk.fillH = H; wk.fillh = h;
    }
    // per output column: the two source columns, the smoothstep weight, grille, vignette, glow column
    const size_t nW = size_t(W);
    std::vector<int> xa(nW), xb(nW), gx(nW);
    std::vector<float> xt(nW), vigX(nW), maskW(nW * 3);
    const float cx = W * 0.5f, cy = H * 0.5f;
    for (int X = 0; X < W; ++X) {
        const float u = (X + 0.5f) / sx - 0.5f;
        const int x0 = int(std::floor(u)); float t = u - float(x0);
        xt[size_t(X)] = t * t * (3 - 2 * t);
        xa[size_t(X)] = std::clamp(x0, 0, w - 1); xb[size_t(X)] = std::clamp(x0 + 1, 0, w - 1);
        gx[size_t(X)] = std::min(int(X / sx), w - 1);
        const float vx = (X - cx) / cx; vigX[size_t(X)] = vx * vx;
        for (int c = 0; c < 3; ++c) maskW[size_t(X) * 3 + c] = (X % 3 == c) ? 1.0f : 0.72f;
    }
    parallelRows(H, [&](int Y0, int Y1) {
        std::vector<float> beam(size_t(w) * 3);
        for (int Y = Y0; Y < Y1; ++Y) {
            const int r0 = wk.fillR0[size_t(Y)];
            const float* E = &wk.fillE[size_t(Y) * kE];
            const float* R[4];
            for (int k = 0; k < 4; ++k) R[k] = &wk.lin[size_t(std::clamp(r0 + k, 0, h - 1)) * w * 3];
            for (int i = 0; i < w * 3; ++i) {
                float val = 0;
                for (int k = 0; k < 4; ++k) {
                    const float a = R[k][i];
                    val += a * E[k * (kLev + 1) + int(std::min(a, 1.0f) * kLev)];
                }
                beam[size_t(i)] = val;
            }
            const int gy = std::clamp(int((Y + 0.5f) / sy), 0, h - 1);
            const float vy = (Y - cy) / cy, vy2 = vy * vy;
            const float* G = &wk.glow[size_t(gy) * w * 3];
            uint8_t* o = out + size_t(Y) * W * 3;
            for (int X = 0; X < W; ++X) {
                const float vig = 1.0f - 0.12f * (vigX[size_t(X)] + vy2);
                const float* A = &beam[size_t(xa[size_t(X)]) * 3];
                const float* B = &beam[size_t(xb[size_t(X)]) * 3];
                const float* g = &G[size_t(gx[size_t(X)]) * 3];
                const float t = xt[size_t(X)];
                for (int c = 0; c < 3; ++c) {
                    float val = A[c] + (B[c] - A[c]) * t;
                    val = (val * maskW[size_t(X) * 3 + c] + 0.10f * g[c]) * vig;
                    o[X * 3 + c] = L.toGam[std::clamp(int(val * 4096), 0, 4096)];
                }
            }
        }
    });
}

} // namespace dm::picture
