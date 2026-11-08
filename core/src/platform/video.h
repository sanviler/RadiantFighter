#pragma once
// Video layer of the viewer: shows an RGB frame buffer at the native resolution
// (320x200 PAL lowres), scaled by an integer factor (sharp pixels).
//
#include "formats/palette.h"
#include <SDL.h>
#include <string>
#include <vector>

namespace dm {

class Video {
public:
    // window = half the desktop resolution, resizable; the picture is always scaled by an integer factor of the
    // window or (fullscreen) desktop resolution, centred with black borders. `scale` is ignored (kept for the API).
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
    int crtW_ = 0, crtH_ = 0, crtScale_ = 0;
    std::vector<float> lin_, glow_, rows_;
    std::vector<uint8_t> crtOut_;
    void renderCrt(int scale);
};

} // namespace dm
