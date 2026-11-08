// RadiantFighter: CD audio of the Shadow Fighter CD32 disc, from WAV files made by the user.
//
// The CD music (option "MUSIC: CD") plays from the "audio" folder next to the executable, which the port creates
// with a README.txt inside: track02.wav
// (the first audio track; track 1 of the disc is the game data) to track13.wav, exactly these names,
// 44.1 kHz 16-bit stereo. Any WAV with these names plays (the user's own music too). Which track, as on the CD32:
// selection and menus track 3, each fight the track of its music number (table at $8CA8E of CDGAME.BIN), the
// ending track 13. With CD music the Paula music of the floppy version is not played (except in the intro);
// the fight always has the stage (background) sounds and the sound effects. "MUSIC: PAULA" = original game.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace amiga { class Machine; }

namespace rf {

class CdAudio {
public:
    static CdAudio& instance();
    ~CdAudio();
    // reads the WAV files track02.wav ... track13.wav of "folder" (created if missing, with README.txt); true if at least one is there
    bool locate(const std::string& folder, std::string& msg);
    bool available() const { return available_; }
    void setEnabled(bool on) { enabled_ = on && available_; if (!enabled_) playing_ = false; }
    bool enabled() const { return enabled_; }

    // game side (loader hooks)
    void requestMusic(int musicNumber);          // the game chose the music of the next fight
    void requestTrack(int track);                // menus (3), ending (13)
    // once per frame: follows the game's music switch, sound option and fade
    void update(const amiga::Machine& m);
    // adds the CD audio to a block of 16-bit stereo samples at "rate" Hz
    void mix(int16_t* stereo, size_t frames, int rate);
    int currentTrack() const { return track_; }
    // music volume 0-256 (option), on top of the game's fade
    void setGain(int g) { gain_ = g < 0 ? 0 : (g > 256 ? 256 : g); }
    void stop() { track_ = 0; playing_ = false; }

private:
    struct Track { std::string file; uint64_t offset = 0, bytes = 0; };
    Track tracks_[100];
    bool available_ = false, enabled_ = false;
    int track_ = 0;                 // track being played (0 = none)
    bool playing_ = false;          // false = paused (the game has no music at this moment)
    int volume_ = 256;
    int gain_ = 256;              // 0-256, follows the game's music fade
    uint64_t pos_ = 0;              // byte position in the track
    double frac_ = 0;
    std::FILE* f_ = nullptr;
    std::string openFile_;
    std::vector<int16_t> buf_;
    bool readFrames(size_t n, std::vector<int16_t>& out);
};

} // namespace rf
