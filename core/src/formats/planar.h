#pragma once
// Bob/tile graphics (gfx*.epf entries): NON-interleaved bitplanes, one after the other,
// rows aligned to words. The game uses 4 planes (16 colours) for objects.
// The width is not stored in the entry: it comes from the descriptors (lev*_000, 16-byte records:
// flags, duration, group, hotspot dx/dy, width, height).
#include "formats/common.h"

namespace dm {

struct PlanarImage {
    int width = 0, height = 0;
    std::vector<uint8_t> indices;
};

PlanarImage decodePlanar(const Bytes& data, int widthWords, int height, int planes = 4);

// Guesses the width (in words) as the one with the best match between adjacent rows.
// A heuristic for tools and previews; the renderer uses the descriptors.
int guessWidthWords(const Bytes& data, int planes = 4);

} // namespace dm
