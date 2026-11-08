#pragma once
// Reading and writing the dialogue texts of the txt*.epf archives (format in text.h), so that the texts of a
// language can be corrected and put back into the game.
//
// "Game text" is a message as the game stores it: CP437 characters, with the letters m, w, M and W written as
// the glyph pairs of the font ("%&", "#$", "^_", "\]"). A message is a sequence of words (from the dictionary)
// and single separator characters (space and punctuation); two words never follow each other.
//
// The dictionary keeps the words in the order of their first use, as the original files do, so encoding the
// decoded messages gives back the original bytes. The index table points at messages by their position in the
// stream (in 16-bit words); after a change the positions are moved to the new starts of the same messages.
#include "formats/common.h"
#include <string>
#include <vector>

namespace dm::text {

// separator characters of the token stream
bool isSeparator(uint8_t c);

// messages of one language, in game text
std::vector<std::string> decode(const Bytes& stream, const Bytes& dictionary);
// the opposite; throws FormatError if a message is empty or the stream gets too large
void encode(const std::vector<std::string>& messages, Bytes& stream, Bytes& dictionary);

// one entry of the index table: a message of a list (group), with its type, id and extra bytes
struct IndexEntry {
    uint8_t group = 0, type = 0, id = 0;
    size_t message = 0;                 // number of the message
    size_t offset = 0;                  // offset of the position field in the table
};
// entries of the index table; throws FormatError if the table does not match the stream
std::vector<IndexEntry> parseIndex(const Bytes& index, const Bytes& stream);
// the index table for a new stream holding the same messages
Bytes remapIndex(const Bytes& index, const Bytes& oldStream, const Bytes& newStream);

// game text <-> UTF-8
// (widePairs = false for the texts of the program drawn with other fonts, where m and w are plain letters)
std::string toUtf8(const std::string& game, bool widePairs = true);
// converts UTF-8 to game text. Typographic quotes, dashes and ellipses become their plain forms. Returns false
// and the offending character in bad if a character has no CP437 code or would be read as a glyph pair.
bool fromUtf8(const std::string& utf8, std::string& game, std::string& bad, bool widePairs = true);

} // namespace dm::text
