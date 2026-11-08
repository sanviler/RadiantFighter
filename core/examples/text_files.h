#pragma once
// Text files for correcting the game's texts: an archive of messages is written as a plain UTF-8 file that a
// person can read and edit, and an edited file is put back into the game.
//
//   @file txt11000.epf 3          archive and language (0 EN, 1 FR, 2 DE, 3 IT); do not edit
//   ### 1211 1212 1213            numbers of the messages that have this text (identical texts are written once)
//   # comment lines                context and the English text, only for reference
//   Via delle Mura Ovest          the text: one or more lines, joined with a space
//
// A heading may end with {xxxxxxxx}: the first 8 hex digits of the SHA-256 of the original text. A correction
// with this check is applied only if the original is the same (used for the corrections shipped with
// Lightmere, which hold only the changed messages). The archive "MAIN.BIN" holds a few fixed-length texts of
// the program, numbered by their offset in hexadecimal. Other names (such as "lightmere", Lightmere's own
// texts) are read but not applied.
#include "formats/common.h"
#include <string>
#include <vector>

namespace dm {
class GameFiles;
}

namespace dm::text {

struct FileEntry {
    std::vector<unsigned> ids;
    std::string text;                   // UTF-8, as written in the file
    std::string check;                  // optional: hash of the original text
    int line = 0;
};

struct TextFile {
    std::string archive;                // "txt11000.epf", "MAIN.BIN"
    int lang = 3;
    std::vector<FileEntry> entries;
    std::string path;                   // where it was read from (messages)
};

// the fixed-length texts of MAIN.BIN offered for correction
struct ProgramField {
    uint32_t offset;                    // in the loaded program (from its start, as in fixes.cpp)
    uint32_t length;
    bool centred;
    const char* what;                   // description (Italian, for the exported file)
};
const std::vector<ProgramField>& programFields(int lang);

std::string shortHash(const std::string& gameText);

// a text file (it may hold several archives, each starting with its @file line); false and a message (with
// the line number) if it cannot be read
bool parseFile(const std::string& content, std::vector<TextFile>& out, std::string& err);
// every *.txt file of a folder, or a single file
bool loadTexts(const std::string& path, std::vector<TextFile>& out, std::string& err);

// a readable file for one archive of the game files: lang = language of the texts, refLang = language shown
// in the comments for reference (-1: none)
std::string exportArchive(const GameFiles& files, const std::string& archive, int lang, int refLang);
// the fixed-length texts of the program
std::string exportProgram(const GameFiles& files, int lang);

struct ApplyReport {
    int changed = 0;                    // messages changed
    std::vector<std::string> errors;    // corrections that could not be applied
    std::vector<std::string> warnings;  // applied, but worth a look (longer than every original, ...)
};

// puts the corrected messages of the txt archives into the game files (in memory); MAIN.BIN texts are left
// to programPatches
ApplyReport apply(const std::vector<TextFile>& files, GameFiles& gameFiles);
// corrected fixed-length texts of the program: (offset, bytes) to write into the loaded program, with the
// original text expected there
struct ProgramPatch { uint32_t offset; std::string original, text; };
std::vector<ProgramPatch> programPatches(const std::vector<TextFile>& files, const GameFiles& gameFiles,
                                         ApplyReport& report);

// a corrections file with only the messages of the edited files that differ from the original (with checks),
// for shipping with Lightmere
std::string differences(const std::vector<TextFile>& files, const GameFiles& gameFiles, ApplyReport& report);

} // namespace dm::text
