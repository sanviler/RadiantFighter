#include "formats/text_codec.h"
#include "formats/text.h"
#include <map>
#include <unordered_map>

namespace dm::text {

namespace {
constexpr std::pair<char, const char*> kWide[] = {{'m', "%&"}, {'w', "#$"}, {'M', "^_"}, {'W', "\\]"}};

// message starts (position in words) -> message number
std::map<size_t, size_t> messageStarts(const Bytes& stream) {
    std::map<size_t, size_t> starts;
    size_t n = 0;
    starts[1] = n++;
    const size_t words = stream.size() / 2;
    for (size_t k = 1; k < words; ++k)
        if (be16(&stream[k * 2]) & 0x4000) starts[k + 1] = n++;
    return starts;
}

// decodes one UTF-8 code point; false at an invalid sequence
bool nextCodePoint(const std::string& s, size_t& i, char32_t& cp) {
    const auto c = uint8_t(s[i]);
    int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
    if (extra < 0 || i + size_t(extra) >= s.size() + (extra ? 0 : 1)) return false;
    cp = extra == 0 ? c : extra == 1 ? (c & 0x1F) : extra == 2 ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k <= extra; ++k) {
        const auto d = uint8_t(s[i + size_t(k)]);
        if ((d >> 6) != 2) return false;
        cp = (cp << 6) | (d & 0x3F);
    }
    i += size_t(extra) + 1;
    return true;
}
} // namespace

bool isSeparator(uint8_t c) {
    switch (c) {
        case ' ': case '!': case '"': case '(': case ')': case '*': case '+': case ',': case '-': case '.':
        case ':': case ';': case '=': case '?': return true;
        default: return false;
    }
}

std::vector<std::string> decode(const Bytes& stream, const Bytes& dict) {
    std::vector<std::string> words;
    std::string cur;
    for (uint8_t c : dict) {
        if (c) cur += char(c);
        else { words.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) words.push_back(cur);
    std::vector<std::string> msgs;
    cur.clear();
    for (size_t k = 2; k + 1 < stream.size(); k += 2) {
        const uint16_t w = be16(&stream[k]);
        if (w & 0x8000) cur += char(w & 0xFF);
        else {
            const size_t i = w & 0x3FFF;
            if (i >= words.size()) throw FormatError("texts: word outside the dictionary");
            cur += words[i];
        }
        if (w & 0x4000) { msgs.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) throw FormatError("texts: last message not terminated");
    return msgs;
}

void encode(const std::vector<std::string>& messages, Bytes& stream, Bytes& dict) {
    std::vector<uint16_t> tokens;
    std::unordered_map<std::string, uint16_t> index;
    std::vector<std::string> words;
    for (const std::string& m : messages) {
        if (m.empty()) throw FormatError("texts: empty message");
        size_t i = 0;
        while (i < m.size()) {
            if (isSeparator(uint8_t(m[i]))) { tokens.push_back(uint16_t(0x8000 | uint8_t(m[i]))); ++i; continue; }
            size_t j = i;
            while (j < m.size() && !isSeparator(uint8_t(m[j]))) ++j;
            const std::string w = m.substr(i, j - i);
            auto it = index.find(w);
            if (it == index.end()) {
                if (words.size() >= 0x4000) throw FormatError("texts: too many different words");
                it = index.emplace(w, uint16_t(words.size())).first;
                words.push_back(w);
            }
            tokens.push_back(it->second);
            i = j;
        }
        tokens.back() |= 0x4000;
    }
    if (tokens.size() + 1 > 0xFFFF) throw FormatError("texts: too long for the game's index (65535 words)");
    stream.clear();
    stream.reserve(tokens.size() * 2 + 2);
    auto put = [&](uint16_t v) { stream.push_back(uint8_t(v >> 8)); stream.push_back(uint8_t(v)); };
    put(uint16_t(words.size()));
    for (uint16_t t : tokens) put(t);
    dict.clear();
    for (const std::string& w : words) { dict.insert(dict.end(), w.begin(), w.end()); dict.push_back(0); }
}

std::vector<IndexEntry> parseIndex(const Bytes& index, const Bytes& stream) {
    // [FF] then lists: [group] entries ([type][id][position: u16][extra bytes...][FF])... [FF]; [FF] at the end
    // (read the same way as the game, routines $05F68 and $060DE)
    const auto starts = messageStarts(stream);
    std::vector<IndexEntry> out;
    size_t p = 1;
    auto need = [&](size_t n) { if (p + n > index.size()) throw FormatError("texts: index table truncated"); };
    need(1);
    while (index[p] != 0xFF) {
        const uint8_t group = index[p++];
        for (;;) {
            need(5);
            IndexEntry e;
            e.group = group;
            e.type = index[p];
            e.id = index[p + 1];
            e.offset = p + 2;
            const auto it = starts.find(be16(&index[p + 2]));
            if (it == starts.end()) throw FormatError("texts: index entry not at the start of a message");
            e.message = it->second;
            out.push_back(e);
            p += 4;
            while (need(1), index[p] != 0xFF) ++p;
            ++p;
            need(1);
            if (index[p] == 0xFF) { ++p; break; }
        }
        need(1);
    }
    return out;
}

Bytes remapIndex(const Bytes& index, const Bytes& oldStream, const Bytes& newStream) {
    const auto entries = parseIndex(index, oldStream);
    std::vector<size_t> newStart;
    for (const auto& [pos, msg] : messageStarts(newStream)) {
        if (msg >= newStart.size()) newStart.resize(msg + 1);
        newStart[msg] = pos;
    }
    Bytes out = index;
    for (const IndexEntry& e : entries) {
        if (e.message >= newStart.size()) throw FormatError("texts: message missing in the new texts");
        const size_t pos = newStart[e.message];
        out[e.offset] = uint8_t(pos >> 8);
        out[e.offset + 1] = uint8_t(pos);
    }
    return out;
}

std::string toUtf8(const std::string& game, bool widePairs) {
    std::string s = game;
    if (widePairs)
        for (auto [letter, pair] : kWide)
            for (size_t p; (p = s.find(pair)) != std::string::npos;) s.replace(p, 2, 1, letter);
    return cp437ToUtf8(s);
}

bool fromUtf8(const std::string& utf8, std::string& game, std::string& bad, bool widePairs) {
    static const std::unordered_map<char32_t, uint8_t> kHigh = [] {
        std::unordered_map<char32_t, uint8_t> m;
        for (int c = 0x80; c < 0x100; ++c) {
            const std::string u = cp437ToUtf8(std::string(1, char(c)));
            size_t i = 0;
            char32_t cp = 0;
            if (nextCodePoint(u, i, cp)) m[cp] = uint8_t(c);
        }
        return m;
    }();
    game.clear();
    for (size_t i = 0; i < utf8.size();) {
        const size_t at = i;
        char32_t cp = 0;
        if (!nextCodePoint(utf8, i, cp)) { bad = "(invalid UTF-8)"; return false; }
        switch (cp) {                                   // typographic forms -> plain characters
            case 0x2018: case 0x2019: case 0x201B: case 0x00B4: case 0x0060: cp = '\''; break;
            case 0x201C: case 0x201D: case 0x201E: case 0x00AB: case 0x00BB: cp = '"'; break;
            case 0x2013: case 0x2014: cp = '-'; break;
            case 0x00A0: case '\t': cp = ' '; break;
            case 0x2026: game += ".."; cp = '.'; break;
            default: break;
        }
        bool wide = false;
        if (widePairs)
            for (auto [letter, pair] : kWide)
                if (cp == char32_t(uint8_t(letter))) { game += pair; wide = true; }
        if (wide) continue;
        if (cp >= 0x20 && cp < 0x7F) {
            if (widePairs && (cp == '%' || cp == '&' || cp == '#' || cp == '$' || cp == '^' || cp == '_' || cp == '\\' || cp == ']')) {
                bad = utf8.substr(at, i - at);
                return false;
            }
            game += char(cp);
            continue;
        }
        const auto it = kHigh.find(cp);
        if (it == kHigh.end()) { bad = utf8.substr(at, i - at); return false; }
        game += char(it->second);
    }
    return true;
}

} // namespace dm::text
