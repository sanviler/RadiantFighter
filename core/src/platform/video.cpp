#include "platform/video.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dm {

void Video::hints() {
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
#ifdef SDL_HINT_WINDOWS_DPI_SCALING
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "0");
#endif
}

bool Video::init(const char* title, int lw, int lh, int /*scale*/) {
    hints();
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
    if (overlayTex_) SDL_DestroyTexture(overlayTex_);
    overlayTex_ = nullptr;
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
    int W = 0, H = 0, s = 1;
    if (mode_ == Mode::Fill) picture::fillSize(w_, h_, ow, oh, W, H);
    else picture::integerSize(w_, h_, ow, oh, W, H, s);
    const bool plain = mode_ == Mode::PixelPerfect ? !(crt_ && s >= 2) : (W == w_ * s && H == h_ * s && !crt_);
    // drawn in output pixels: the logical size (kept for the mouse) is switched off while drawing
    SDL_RenderSetLogicalSize(ren_, 0, 0);
    const SDL_Rect dst{(ow - W) / 2, (oh - H) / 2, W, H};
    if (plain) {
        void* pixels;
        int pitch;
        if (SDL_LockTexture(tex_, nullptr, &pixels, &pitch) == 0) {
            for (int y = 0; y < h_; ++y)
                std::memcpy(static_cast<uint8_t*>(pixels) + size_t(y) * pitch, &frame_[size_t(y) * w_], size_t(w_) * 3);
            SDL_UnlockTexture(tex_);
        }
        SDL_RenderCopy(ren_, tex_, nullptr, &dst);
    } else {
        if (!crtTex_ || crtW_ != W || crtH_ != H) {
            if (crtTex_) SDL_DestroyTexture(crtTex_);
            crtTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, W, H);
            crtW_ = W; crtH_ = H;
        }
        out_.resize(size_t(W) * H * 3);
        if (mode_ == Mode::PixelPerfect) picture::integerCrt(frame_.data(), w_, h_, s, out_.data(), work_);
        else if (crt_) picture::fillCrt(frame_.data(), w_, h_, W, H, out_.data(), work_);
        else picture::fillSharp(frame_.data(), w_, h_, W, H, out_.data());
        void* pixels; int pitch;
        if (crtTex_ && SDL_LockTexture(crtTex_, nullptr, &pixels, &pitch) == 0) {
            for (int y = 0; y < H; ++y) std::memcpy(static_cast<uint8_t*>(pixels) + size_t(y) * pitch, &out_[size_t(y) * W * 3], size_t(W) * 3);
            SDL_UnlockTexture(crtTex_);
        }
        SDL_RenderCopy(ren_, crtTex_, nullptr, &dst);          // 1:1
    }
    if (overlayOn_ && overlayTex_) {
        const int s = std::max(1, overlayScale_);
        const SDL_Rect od{(ow - overlayW_ * s) / 2, (oh - overlayH_ * s) / 2, overlayW_ * s, overlayH_ * s};
        const SDL_Rect os{0, 0, overlayW_, overlayH_};
        SDL_RenderCopy(ren_, overlayTex_, &os, &od);
    }
    SDL_RenderPresent(ren_);
    SDL_RenderSetLogicalSize(ren_, w_, h_);
    SDL_RenderSetIntegerScale(ren_, mode_ == Mode::PixelPerfect ? SDL_TRUE : SDL_FALSE);
}

void Video::setOverlay(const std::vector<Rgb>& px, int w, int h, int scale) {
    if (w <= 0 || h <= 0 || px.size() < size_t(w) * size_t(h)) { overlayOn_ = false; return; }
    if (!overlayTex_ || overlayTexW_ < w || overlayTexH_ < h) {
        if (overlayTex_) SDL_DestroyTexture(overlayTex_);
        overlayTexW_ = std::max(w, overlayTexW_); overlayTexH_ = std::max(h, overlayTexH_);
        overlayTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, overlayTexW_, overlayTexH_);
        if (overlayTex_) SDL_SetTextureScaleMode(overlayTex_, SDL_ScaleModeNearest);
    }
    if (!overlayTex_) { overlayOn_ = false; return; }
    const SDL_Rect r{0, 0, w, h};
    SDL_UpdateTexture(overlayTex_, &r, px.data(), w * 3);
    overlayW_ = w; overlayH_ = h; overlayScale_ = scale; overlayOn_ = true;
}

void Video::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(win_, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void Video::setTitle(const std::string& t) { SDL_SetWindowTitle(win_, t.c_str()); }

} // namespace dm
