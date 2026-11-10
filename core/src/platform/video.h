#pragma once
// Video layer of the viewer: shows an RGB frame buffer at the native resolution
// (320x200 PAL lowres), scaled by an integer factor (sharp pixels).
//
#include "formats/palette.h"
#include "platform/picture.h"
#include <SDL.h>
#include <string>
#include <vector>

namespace dm {

class Video {
public:
    // Picture modes:
    // - PixelPerfect: the largest integer scale of the window or (fullscreen) desktop, centred, black borders;
    //   with the CRT filter in the style of the "crt-pixie" shader computed exactly at that scale;
    // - Fill: as large as the window or desktop allows with the aspect of the picture (any scale), centred;
    //   sharp bilinear pixels, or the CRT filter for non-integer scales (beams averaged over every output row).
    enum class Mode { PixelPerfect, Fill };
    // call before SDL_Init: real pixels on Windows with display scaling (125%, 150%: without it Windows stretches
    // the window and the integer scale is lost)
    static void hints();
    // window = half the desktop resolution, resizable. `scale` is ignored (kept for the API).
    bool init(const char* title, int logicalW, int logicalH, int scale = 3);
    void shutdown();
    void resizeLogical(int w, int h);

    int width() const { return w_; }
    int height() const { return h_; }
    std::vector<Rgb>& frame() { return frame_; }
    void clear(Rgb c = {0, 0, 0});
    void blitIndexed(int x, int y, int w, int h, const uint8_t* idx, const Palette& pal, int transparent = -1);
    void blitRgb(int x, int y, int w, int h, const Rgb* px);

    void present();
    // CRT filter in the style of the "crt-pixie" shader (soft phosphors, scanlines that widen on bright colours,
    // RGB aperture mask, glow, vignette), computed on the CPU at the integer scale of the window
    void setCrt(bool on) { crt_ = on; }
    bool crt() const { return crt_; }
    void setMode(Mode m) { mode_ = m; }
    Mode mode() const { return mode_; }
    // a picture shown over the filtered game picture at an integer scale of its own `scale` (the options panel:
    // never filtered, the same size in every port), centred; it stays until clearOverlay()
    void setOverlay(const std::vector<Rgb>& px, int w, int h, int scale);
    void clearOverlay() { overlayOn_ = false; }
    // size of the window or desktop in real pixels
    void outputSize(int& w, int& h) const { w = h = 0; if (ren_) SDL_GetRendererOutputSize(ren_, &w, &h); }
    void toggleFullscreen();
    void setTitle(const std::string& t);

private:
    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    SDL_Texture* tex_ = nullptr;
    int w_ = 0, h_ = 0;
    std::vector<Rgb> frame_;
    bool fullscreen_ = false;
    bool crt_ = false;
    SDL_Texture* crtTex_ = nullptr;
    Mode mode_ = Mode::PixelPerfect;
    int crtW_ = 0, crtH_ = 0;
    picture::Work work_;
    std::vector<uint8_t> out_;
    SDL_Texture* overlayTex_ = nullptr;
    int overlayTexW_ = 0, overlayTexH_ = 0, overlayW_ = 0, overlayH_ = 0, overlayScale_ = 1;
    bool overlayOn_ = false;
};

} // namespace dm
