// SPDX-License-Identifier: GPL-3.0-or-later
#include "rf/cdaudio.h"
#include "amiga/machine.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace rf {

namespace {
// CD32 version, CDGAME.BIN $8CA8E: audio track for each music number (music number = entry of the fight
// table, +1 for the background sounds); the pairs are equal: the CD music is the same in both modes
constexpr int kTrackOfMusic[24] = {3, 3, 4, 4, 11, 11, 4, 4, 2, 2, 8, 8, 5, 5, 3, 3, 6, 6, 10, 10, 11, 11, 7, 7};
constexpr uint32_t kFadeOn = 0x8BCFE, kFadeLevel = 0x8BCFC;   // music fade: level $20 -> 0

// PCM data of a WAV file (44.1 kHz, 16-bit, stereo): offset and size
bool wavData(const fs::path& p, uint64_t& off, uint64_t& bytes) {
    std::ifstream f(p, std::ios::binary);
    char h[12];
    if (!f.read(h, 12) || std::memcmp(h, "RIFF", 4) || std::memcmp(h + 8, "WAVE", 4)) return false;
    bool fmtOk = false;
    for (;;) {
        char id[4]; uint32_t n = 0;
        if (!f.read(id, 4) || !f.read(reinterpret_cast<char*>(&n), 4)) return false;
        if (!std::memcmp(id, "fmt ", 4)) {
            std::vector<uint8_t> b(n);
            f.read(reinterpret_cast<char*>(b.data()), n);
            const int fmt = b[0] | b[1] << 8, ch = b[2] | b[3] << 8, bits = b[14] | b[15] << 8;
            const uint32_t rate = uint32_t(b[4] | b[5] << 8 | b[6] << 16 | b[7] << 24);
            fmtOk = fmt == 1 && ch == 2 && bits == 16 && rate == 44100;
        } else if (!std::memcmp(id, "data", 4)) {
            off = uint64_t(f.tellg()); bytes = n;
            return fmtOk;
        } else f.seekg(n + (n & 1), std::ios::cur);
    }
}
} // namespace

CdAudio& CdAudio::instance() { static CdAudio c; return c; }
CdAudio::~CdAudio() { if (f_) std::fclose(f_); }

bool CdAudio::locate(const std::string& folder, std::string& msg) {
    available_ = false;
    std::error_code ec;
    fs::create_directories(folder, ec);
    const fs::path readme = fs::path(folder) / "README.txt";
    if (!fs::exists(readme, ec)) {
        std::ofstream r(readme, std::ios::binary);
        r << "RadiantFighter - CD audio tracks\r\n"
             "================================\r\n\r\n"
             "Put here the audio tracks extracted from YOUR original Shadow Fighter CD32 disc.\r\n"
             "They are not included with RadiantFighter and must not be distributed.\r\n\r\n"
             "File names, exactly (track 1 of the disc is the game data, not music):\r\n"
             "  track02.wav, track03.wav, track04.wav, ... track13.wav\r\n\r\n"
             "Format: WAV, 44100 Hz, 16-bit, stereo (the usual result of a CD ripper or of\r\n"
             "\"chdman extractcd\" followed by a split to WAV).\r\n\r\n"
             "Then choose MUSIC: CD in the options menu (Escape). Without the tracks, or with\r\n"
             "MUSIC: PAULA, the game plays its original Amiga music.\r\n";
    }
    int n = 0;
    for (int t = 2; t <= 13; ++t) {
        char name[16]; std::snprintf(name, sizeof name, "track%02d.wav", t);
        uint64_t off = 0, bytes = 0;
        tracks_[t] = Track{};
        if (wavData(fs::path(folder) / name, off, bytes)) { tracks_[t] = {(fs::path(folder) / name).string(), off, bytes}; ++n; }
    }
    available_ = n > 0;
    msg = available_ ? "CD audio: " + std::to_string(n) + " tracks in " + folder : "CD audio: no trackNN.wav in " + folder + " (no music)";
    return available_;
}

void CdAudio::requestMusic(int n) { if (n >= 0 && n < 24) requestTrack(kTrackOfMusic[n]); }

void CdAudio::requestTrack(int t) {
    if (t == track_) return;
    track_ = t; pos_ = 0; frac_ = 0;
}

void CdAudio::update(const amiga::Machine& m) {
    if (!enabled_ || !track_) { playing_ = false; return; }
    playing_ = tracks_[track_].bytes > 0;
    volume_ = m.peek16(kFadeOn) ? std::clamp(int(m.peek16(kFadeLevel)) * 8, 0, 256) : 256;
}

bool CdAudio::readFrames(size_t n, std::vector<int16_t>& out) {
    if (track_ < 2 || track_ > 13) return false;
    const Track& t = tracks_[track_];
    if (!t.bytes) return false;
    if (openFile_ != t.file) {
        if (f_) std::fclose(f_);
        f_ = std::fopen(t.file.c_str(), "rb");
        openFile_ = f_ ? t.file : "";
    }
    if (!f_) return false;
    out.resize(n * 2);
    size_t got = 0;
    while (got < n) {
        if (pos_ + 4 > t.bytes) pos_ = 0;                       // the CD32 code starts the track again
        const size_t want = size_t(std::min<uint64_t>((n - got) * 4, (t.bytes - pos_) & ~uint64_t(3)));
        std::fseek(f_, long(t.offset + pos_), SEEK_SET);
        const size_t r = std::fread(&out[got * 2], 1, want, f_);
        if (r < 4) return false;
        got += r / 4; pos_ += r & ~size_t(3);
    }
    return true;
}

void CdAudio::mix(int16_t* stereo, size_t frames, int rate) {
    if (!enabled_ || !track_ || !playing_ || rate <= 0) return;
    const double step = 44100.0 / rate;
    const size_t need = size_t(frames * step + frac_) + 2;
    const uint64_t start = pos_;
    if (!readFrames(need, buf_)) return;
    double p = frac_;
    for (size_t i = 0; i < frames; ++i, p += step) {
        const size_t k = size_t(p); const double a = p - double(k);
        for (int c = 0; c < 2; ++c) {
            const double s = buf_[k * 2 + size_t(c)] * (1 - a) + buf_[(k + 1) * 2 + size_t(c)] * a;
            const int v = stereo[i * 2 + size_t(c)] + int(s * volume_ / 256 * gain_ / 256 * 0.6);
            stereo[i * 2 + size_t(c)] = int16_t(std::clamp(v, -32768, 32767));
        }
    }
    // keep the position of the first frame not used yet
    const size_t used = size_t(p);
    frac_ = p - double(used);
    const uint64_t len = tracks_[track_].bytes & ~uint64_t(3);
    pos_ = len ? (start + uint64_t(used) * 4) % len : 0;
}

} // namespace rf
