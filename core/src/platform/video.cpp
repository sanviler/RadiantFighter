#include "platform/video.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace dm {

bool Video::init(const char* title, int lw, int lh, int /*scale*/) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return false;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // nearest: sharp pixels
    // window: half the width and half the height of the desktop (at least one picture at 1x); resizable, the
    // picture always scales by an integer factor with black borders around it. Fullscreen uses the desktop mode.
    int ww = lw * 3, wh = lh * 3;
    SDL_DisplayMode dm{};
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) { ww = dm.w / 2; wh = dm.h / 2; }
    ww = std::max(ww, lw); wh = std::max(wh, lh);
    win_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win_) return false;
    SDL_SetWindowMinimumSize(win_, lw, lh);
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
    if (!ren_) return false;
    resizeLogical(lw, lh);
    return true;
}

void Video::resizeLogical(int w, int h) {
    if (w == w_ && h == h_ && tex_) return;
    w_ = w; h_ = h;
    frame_.assign(size_t(w) * h, Rgb{0, 0, 0});
    if (tex_) SDL_DestroyTexture(tex_);
    tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w, h);
    SDL_RenderSetLogicalSize(ren_, w, h);
    SDL_RenderSetIntegerScale(ren_, SDL_TRUE);        // always integer: uniform pixels and CRT scanlines
}

void Video::shutdown() {
    if (tex_) SDL_DestroyTexture(tex_);
    if (crtTex_) SDL_DestroyTexture(crtTex_);
    crtTex_ = nullptr;
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
    tex_ = nullptr; ren_ = nullptr; win_ = nullptr;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void Video::clear(Rgb c) { std::fill(frame_.begin(), frame_.end(), c); }

void Video::blitIndexed(int x0, int y0, int w, int h, const uint8_t* idx, const Palette& pal, int transparent) {
    for (int y = 0; y < h; ++y) {
        int sy = y0 + y;
        if (sy < 0 || sy >= h_) continue;
        for (int x = 0; x < w; ++x) {
            int sx = x0 + x;
            if (sx < 0 || sx >= w_) continue;
            uint8_t v = idx[size_t(y) * w + x];
            if (v == transparent) continue;
            frame_[size_t(sy) * w_ + sx] = pal.c[v];
        }
    }
}

void Video::blitRgb(int x0, int y0, int w, int h, const Rgb* px) {
    for (int y = 0; y < h; ++y) {
        int sy = y0 + y;
        if (sy < 0 || sy >= h_) continue;
        for (int x = 0; x < w; ++x) {
            int sx = x0 + x;
            if (sx >= 0 && sx < w_) frame_[size_t(sy) * w_ + sx] = px[size_t(y) * w + x];
        }
    }
}

void Video::present() {
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
    SDL_RenderClear(ren_);
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(ren_, &ow, &oh);
    const int scale = std::max(1, std::min(ow / std::max(w_, 1), oh / std::max(h_, 1)));
    if (crt_ && scale >= 2) {
        renderCrt(scale);                          // exactly the integer scale of the output: copied 1:1, every scanline equal
        SDL_RenderCopy(ren_, crtTex_, nullptr, nullptr);
    } else {
        void* pixels;
        int pitch;
        if (SDL_LockTexture(tex_, nullptr, &pixels, &pitch) == 0) {
            for (int y = 0; y < h_; ++y)
                std::memcpy(static_cast<uint8_t*>(pixels) + size_t(y) * pitch, &frame_[size_t(y) * w_], size_t(w_) * 3);
            SDL_UnlockTexture(tex_);
        }
        SDL_RenderCopy(ren_, tex_, nullptr, nullptr);
    }
    SDL_RenderPresent(ren_);
}

namespace {
// runs f(begin, end) on slices of [0, n) in parallel (the CRT filter is too slow on one core at 4x and above)
template <class F> void parallelRows(int n, F f) {
    const int t = std::clamp(int(std::thread::hardware_concurrency()), 1, 8);
    if (t == 1) { f(0, n); return; }
    std::vector<std::thread> th;
    for (int i = 0; i < t; ++i) th.emplace_back([&, i] { f(n * i / t, n * (i + 1) / t); });
    for (auto& x : th) x.join();
}
} // namespace

// CRT filter. The picture is first made linear (gamma 2.2) and blurred a little (glow); every output row then
// mixes the two nearest source rows with a gaussian beam whose width grows with the brightness; an aperture grille
// (R, G, B columns) and a soft vignette follow; the result goes back to gamma 2.2.
void Video::renderCrt(int s) {
    const int W = w_ * s, H = h_ * s;
    if (!crtTex_ || crtW_ != W || crtH_ != H) {
        if (crtTex_) SDL_DestroyTexture(crtTex_);
        crtTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, W, H);
        crtW_ = W; crtH_ = H; crtScale_ = s;
    }
    static float toLin[256];
    static uint8_t toGam[4097];
    static bool lut = false;
    if (!lut) {
        for (int i = 0; i < 256; ++i) toLin[i] = std::pow(i / 255.0f, 2.2f);
        for (int i = 0; i <= 4096; ++i) toGam[i] = uint8_t(std::lround(std::pow(i / 4096.0f, 1 / 2.2f) * 255));
        lut = true;
    }
    const size_t n = size_t(w_) * h_;
    lin_.resize(n * 3); glow_.resize(n * 3);
    for (size_t i = 0; i < n; ++i) { lin_[i * 3] = toLin[frame_[i].r]; lin_[i * 3 + 1] = toLin[frame_[i].g]; lin_[i * 3 + 2] = toLin[frame_[i].b]; }
    // glow: 5x3 box blur of the linear picture
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x)
            for (int c = 0; c < 3; ++c) {
                float a = 0; int k = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int yy = y + dy; if (yy < 0 || yy >= h_) continue;
                    for (int dx = -2; dx <= 2; ++dx) { const int xx = x + dx; if (xx < 0 || xx >= w_) continue; a += lin_[(size_t(yy) * w_ + xx) * 3 + c]; ++k; }
                }
                glow_[(size_t(y) * w_ + x) * 3 + c] = a / float(k);
            }
    // horizontal pass: each source row at output width, with a soft (linear) reconstruction between pixels
    rows_.resize(size_t(h_) * W * 3);
    parallelRows(h_, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
        const float* src = &lin_[size_t(y) * w_ * 3];
        float* dst = &rows_[size_t(y) * W * 3];
        for (int X = 0; X < W; ++X) {
            const float u = (X + 0.5f) / float(s) - 0.5f;
            int x0 = int(std::floor(u)); float t = u - float(x0);
            t = t * t * (3 - 2 * t);                               // smoothstep: sharp centres, soft edges
            const int xa = std::clamp(x0, 0, w_ - 1), xb = std::clamp(x0 + 1, 0, w_ - 1);
            for (int c = 0; c < 3; ++c) dst[X * 3 + c] = src[xa * 3 + c] * (1 - t) + src[xb * 3 + c] * t;
        }
    }
    });
    crtOut_.resize(size_t(W) * H * 3);
    const float cx = W * 0.5f, cy = H * 0.5f;
    // vignette per column and per row, mask per column
    static std::vector<float> vigX, maskW;
    vigX.resize(size_t(W)); maskW.resize(size_t(W) * 3);
    for (int X = 0; X < W; ++X) {
        const float vx = (X - cx) / cx; vigX[size_t(X)] = vx * vx;
        const int sub = (X * 3 / s) % 3;                            // aperture grille column
        for (int c = 0; c < 3; ++c) maskW[size_t(X) * 3 + c] = (sub == c) ? 1.0f : 0.72f;
    }
    constexpr int kLev = 64;
    parallelRows(H, [&](int Y0, int Y1) {
    float ea[kLev + 1], eb[kLev + 1];
    for (int Y = Y0; Y < Y1; ++Y) {
        const float v = (Y + 0.5f) / float(s) - 0.5f;
        const int r0 = int(std::floor(v)); const float d = v - float(r0);
        // beam of each of the two nearest rows: gaussian, width 0.18 (dark) to 0.32 (bright) source rows
        for (int l = 0; l <= kLev; ++l) {
            const float w = 0.18f + 0.14f * std::sqrt(l / float(kLev));
            ea[l] = std::exp(-(d * d) / (2 * w * w)) * 1.75f;
            eb[l] = std::exp(-((1 - d) * (1 - d)) / (2 * w * w)) * 1.75f;
        }
        const int ra = std::clamp(r0, 0, h_ - 1), rb = std::clamp(r0 + 1, 0, h_ - 1);
        const float* A = &rows_[size_t(ra) * W * 3];
        const float* B = &rows_[size_t(rb) * W * 3];
        const int gy = std::clamp(int((Y + 0.5f) / float(s)), 0, h_ - 1);
        const float vy = (Y - cy) / cy, vy2 = vy * vy;
        const float* G = &glow_[size_t(gy) * w_ * 3];
        uint8_t* out = &crtOut_[size_t(Y) * W * 3];
        for (int X = 0; X < W; ++X) {
            const float vig = 1.0f - 0.12f * (vigX[size_t(X)] + vy2);
            const float* g = &G[size_t(std::min(X / s, w_ - 1)) * 3];
            for (int c = 0; c < 3; ++c) {
                const float a = A[X * 3 + c], b = B[X * 3 + c];
                float val = a * ea[int(std::min(a, 1.0f) * kLev)] + b * eb[int(std::min(b, 1.0f) * kLev)];
                val = (val * maskW[size_t(X) * 3 + c] + 0.10f * g[c]) * vig;
                out[X * 3 + c] = toGam[std::clamp(int(val * 4096), 0, 4096)];
            }
        }
    }
    });
    void* pixels; int pitch;
    if (SDL_LockTexture(crtTex_, nullptr, &pixels, &pitch) == 0) {
        for (int y = 0; y < H; ++y) std::memcpy(static_cast<uint8_t*>(pixels) + size_t(y) * pitch, &crtOut_[size_t(y) * W * 3], size_t(W) * 3);
        SDL_UnlockTexture(crtTex_);
    }
}

void Video::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(win_, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void Video::setTitle(const std::string& t) { SDL_SetWindowTitle(win_, t.c_str()); }

} // namespace dm
