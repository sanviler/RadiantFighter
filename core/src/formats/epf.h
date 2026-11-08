#pragma once
// Core Design ".epf" archives:
//   0  "EPF"            signature
//   3  u8   n           number of entries
//   4  u32  size        total size of the file (BE)
//   8  n x (u32 off, u32 len)
// Every entry is RNC1 packed or raw; an 8-byte "RNC\0" entry is an empty slot.
// The game addresses entries by (file name, index): see the loader at $0A44A in MAIN.BIN.
#include "formats/common.h"

namespace dm {

class EpfArchive {
public:
    explicit EpfArchive(Bytes data);
    static EpfArchive open(const std::string& path) { return EpfArchive(readFile(path)); }

    size_t count() const { return entries_.size(); }
    bool isEmpty(size_t i) const;
    // Returns the unpacked entry (empty for an empty slot).
    Bytes entry(size_t i) const;
    // the entry as stored in the archive (packed or not)
    Bytes raw(size_t i) const;

    // a new archive from entries as they are to be stored
    static Bytes build(const std::vector<Bytes>& rawEntries);
    // an entry stored without packing ("RNC" method 0, understood by the game's decruncher)
    static Bytes stored(const Bytes& data);

private:
    struct Entry { uint32_t off, len; };
    Bytes data_;
    std::vector<Entry> entries_;
};

} // namespace dm
