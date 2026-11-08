#pragma once
// AmigaDOS disk images (ADF, OFS and FFS): the files of a disk, by path ("dir/name").
// Extracted from Lightmere's GameFiles; generic for any AmigaDOS disk. Non-DOS disks (custom trackloaders)
// return false: read those by sector offset instead (see readSectors).
#include "formats/common.h"
#include <map>
#include <string>

namespace dm::adf {

// true and the files of the disk (directories walked, names as stored); false and a message otherwise
bool listFiles(const Bytes& image, std::map<std::string, Bytes>& out, std::string& err);
// raw bytes of a disk region (for games with their own trackloader): offset and length in bytes
Bytes readSectors(const Bytes& image, size_t offset, size_t length);

} // namespace dm::adf
