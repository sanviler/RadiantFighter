// RadiantFighter - Shadow Fighter data formats (music modules, fighter containers).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rf {

using Bytes = std::vector<uint8_t>;

struct Instrument { uint16_t volume, lengthWords, loopWords, loopStartWords; };

// Music module (layout from the replayer init at $8AFE6).
struct MusicModule {
    uint16_t words[4];              // seq = w0, table = w0+w1, patterns = table+w2, samples = patterns+w3
    std::vector<Instrument> instruments;
    uint32_t seq, table, patterns, samples;
};
std::optional<MusicModule> parseMusic(const Bytes& d);
// Converts to a ProTracker M.K. module. periods = 37 words read at $8B20E of the main executable.
Bytes musicToMod(const Bytes& d, const MusicModule& m, const uint16_t periods[37], const std::string& title);

// Fighter container: name, 9 offsets (o0..o8), graphics parts and frames.
struct Part { uint32_t offset; uint16_t bytesPerRow, height, unknown, planeSize; uint32_t maskOffset; };
struct FramePiece { uint16_t part; int16_t dx, dy; };
struct Fighter {
    std::string name;
    uint32_t o[9];
    std::vector<Part> parts;                      // 256 records
    std::vector<std::vector<FramePiece>> frames;
    uint32_t gfxStart, gfxEnd;                    // file[o4+768 .. o5]
};
std::optional<Fighter> parseFighter(const Bytes& d);
// Decodes one part to chunky pixels (0-15) and a mask (0/1), width = bytesPerRow*8.
void decodePart(const Bytes& d, const Fighter& f, const Part& p, std::vector<uint8_t>& pix, std::vector<uint8_t>& mask);

} // namespace rf
