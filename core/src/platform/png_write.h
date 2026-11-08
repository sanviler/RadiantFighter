#pragma once
// PNG writer (8-bit RGB) without external libraries: PNG filters per row and deflate compression with fixed
// Huffman codes and LZ77 (32 KB window). For the screenshots (F12): scaled-up pixel art compresses well
// (repeated rows, flat colours).
#include <cstdint>
#include <string>
#include <vector>

namespace dm {

// rgb: width * height * 3 bytes, rows from the top. false if the file cannot be written.
bool writePng(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb);

} // namespace dm
