// amigacore_selftest [disk.adf] [exe]: checks the core on a user's own files (nothing is bundled).
//   - with an ADF: lists its files (AmigaDOS disks) and unpacks the RNC ones;
//   - with an executable (unpacked, or RNC packed): loads and relocates its hunks at $200000.
#include "formats/adf.h"
#include "formats/hunk.h"
#include "formats/rnc1.h"
#include <cstdio>
#include <map>

int main(int argc, char** argv) {
    using namespace dm;
    int bad = 0;
    for (int i = 1; i < argc; ++i) {
        const Bytes data = readFile(argv[i]);
        std::map<std::string, Bytes> files;
        std::string err;
        if (adf::listFiles(data, files, err)) {
            std::printf("%s: AmigaDOS disk, %zu files\n", argv[i], files.size());
            for (const auto& [name, f] : files) std::printf("  %-32s %8zu%s\n", name.c_str(), f.size(),
                                                           f.size() > 4 && f[0] == 'R' && f[1] == 'N' && f[2] == 'C' ? "  RNC" : "");
            continue;
        }
        Bytes exe = data;
        if (rnc::isPacked(exe.data(), exe.size())) exe = rnc::unpack(exe);
        hunk::Loaded h;
        if (hunk::load(exe, 0x200000, h, err))
            std::printf("%s: executable, %zu hunks, %zu bytes, %zu relocations\n", argv[i], h.start.size(), h.image.size(), h.relocations);
        else { std::printf("%s: %s\n", argv[i], err.c_str()); ++bad; }
    }
    return bad ? 1 : 0;
}
