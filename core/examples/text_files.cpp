#include "formats/text_files.h"
#include "formats/epf.h"
#include "formats/game_files.h"
#include "formats/rnc1.h"
#include "formats/text_codec.h"
#include "platform/sha256.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace dm::text {

namespace {

// the program in MAIN.BIN starts after the hunk header (HUNK_HEADER with one hunk, then HUNK_CODE and its size)
constexpr size_t kHunkData = 0x20;

// the messages of every language of a txt archive
struct Archive {
    std::vector<Bytes> raw;                         // entries as stored
    Bytes stream[4], dict[4], index[4];
    std::vector<std::string> msgs[4];
    std::vector<IndexEntry> entries[4];
};

Archive loadArchive(const GameFiles& files, const std::string& name) {
    const EpfArchive epf(files.read(name));
    if (epf.count() < 12) throw FormatError(name + ": not a text archive");
    Archive a;
    for (size_t i = 0; i < epf.count(); ++i) a.raw.push_back(epf.raw(i));
    for (int l = 0; l < 4; ++l) {
        a.stream[l] = epf.entry(size_t(l) * 3);
        a.dict[l] = epf.entry(size_t(l) * 3 + 1);
        a.index[l] = epf.entry(size_t(l) * 3 + 2);
        a.msgs[l] = decode(a.stream[l], a.dict[l]);
        a.entries[l] = parseIndex(a.index[l], a.stream[l]);
    }
    return a;
}

size_t codePoints(const std::string& utf8) {
    size_t n = 0;
    for (unsigned char c : utf8) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string trimRight(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    return s;
}

std::string trimLeft(std::string s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

bool isNameGroup(uint8_t g) { return g == 0xFA || g == 0xFB || g == 0xFC || g == 0x81 || g == 0x01; }

std::string stripEnd(std::string s) {
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    return s;
}

// a short description of where each message is used (Italian, for the exported file)
std::vector<std::vector<std::string>> contexts(const Archive& a, int lang, bool story) {
    std::vector<std::vector<std::string>> out(a.msgs[lang].size());
    std::map<int, std::string> verb, object;
    for (const IndexEntry& e : a.entries[lang]) {
        const std::string t = stripEnd(toUtf8(a.msgs[lang][e.message]));
        if (e.group == 0x01) verb[e.id] = t;
        if (e.group == 0xFA) object[e.id] = t;
    }
    auto verbName = [&](int v) {
        const auto it = verb.find(v);
        return it != verb.end() ? "«" + it->second + "»" : "azione n. " + std::to_string(v);
    };
    auto objectName = [&](int o) {
        const auto it = object.find(o);
        return it != object.end() ? "«" + it->second + "»" : "oggetto n. " + std::to_string(o);
    };
    for (const IndexEntry& e : a.entries[lang]) {
        const std::string id = std::to_string(e.id);
        std::string s;
        if (story) {
            if (e.group == 0x00) s = "Racconto (introduzione e finale), pagina " + std::to_string(e.id + 1);
            else if (e.id >= 140 && e.id <= 142) s = "Testo all'ingresso nel livello " + std::to_string(e.id - 139);
            else s = "Menu iniziale, voce n. " + id;
        } else {
            switch (e.group) {
                case 0xF9: s = "Consiglio del Re (sfera di cristallo)"; break;
                case 0xFA: s = "Nome dell'oggetto n. " + id; break;
                case 0xFB: s = "Nome di personaggio o creatura n. " + id; break;
                case 0xFC: s = "Nome generico dell'oggetto n. " + id + " (prima di riconoscerlo)"; break;
                case 0xFD:
                    s = "Risposta a " + verbName(e.type) +
                        (e.id >= 253 ? " (parola magica)" : " su " + objectName(e.id));
                    break;
                case 0x58: case 0x59: case 0x5A:
                    s = "Spiegazione del comando " + verbName(e.id) +
                        (e.group == 0x58 ? " (oggetti che porti)" : e.group == 0x59 ? " (oggetti qui intorno)"
                                                                                     : " (oggetti di un cadavere)");
                    break;
                case 0x81: s = "Nome del luogo, stanza " + id; break;
                case 0x01: s = "Voce di menu o messaggio del gioco, n. " + id; break;
                default: {
                    const std::string who = "Personaggio del gruppo " + std::to_string(e.group) + ": ";
                    if (e.type == 0) s = who + "frase n. " + id;
                    else if (e.type == 1) s = who + "risposta a " + verbName(1) + " (argomento n. " + id + ")";
                    else if (e.id == 254) s = who + "risposta a " + verbName(e.type) + " (sul personaggio)";
                    else s = who + "risposta a " + verbName(e.type) + " con " + objectName(e.id);
                }
            }
        }
        out[e.message].push_back(s);
    }
    return out;
}

const char* kLanguageNames[4] = {"inglese", "francese", "tedesco", "italiano"};

std::string header(const std::string& title, const std::string& archive, int lang, bool program) {
    std::ostringstream o;
    o << "# Lightmere - testi di Darkmere da correggere: " << title << "\n"
      << "# Lingua: " << kLanguageNames[lang & 3] << ". File del gioco: " << archive << "\n"
      << "#\n"
      << "# Come correggere\n"
      << "# - Ogni messaggio comincia con una riga \"###\" e i suoi numeri, seguita da righe di commento \"#\"\n"
      << "#   (dove compare nel gioco e il testo inglese, solo come riferimento). Non modificare queste righe.\n"
      << "# - Il testo da correggere e' la riga sotto i commenti. Puoi andare a capo: le righe di uno stesso\n"
      << "#   messaggio vengono unite con uno spazio. Le righe vuote tra i messaggi non contano.\n"
      << "# - I testi uguali sono scritti una volta sola, con tutti i loro numeri: la correzione vale per tutti.\n"
      << "# - Non cancellare messaggi e non lasciarli vuoti; non cambiare la riga \"@file\".\n";
    if (program)
        o << "# - Questi testi hanno una lunghezza fissa (indicata nel commento) e usano un altro carattere:\n"
          << "#   restano in maiuscolo; il testo viene centrato o allineato da Lightmere. Nella riga dei\n"
          << "#   salvataggi i segni + e % sono i posti delle cifre: devono restare dove sono.\n";
    else
        o << "# - Il carattere del gioco non ha tutte le lettere: niente E maiuscola accentata (si scrive E'),\n"
          << "#   niente simboli % & # $ ^ _ \\ ]. Virgolette e apostrofi tipografici (“ ” ‘ ’), lineette e puntini\n"
          << "#   (…) vanno bene: diventano \" ' - ... del gioco.\n";
    o << "# - Salva il file con la codifica UTF-8 (quella predefinita di quasi tutti gli editor).\n"
      << "\n@file " << archive << " " << lang << "\n\n";
    return o.str();
}

} // namespace

const std::vector<ProgramField>& programFields(int lang) {
    static const std::vector<ProgramField> kItalian = {
        {0x1839A, 40, true, "Schermata d'inizio livello"},
        {0x18B3A, 40, false, "Riga dei salvataggi (+ e % = cifre)"},
    };
    static const std::vector<ProgramField> kNone;
    return lang == 3 ? kItalian : kNone;
}

std::string shortHash(const std::string& game) {
    return sha256Hex(reinterpret_cast<const uint8_t*>(game.data()), game.size()).substr(0, 8);
}

bool parseFile(const std::string& content, std::vector<TextFile>& out, std::string& err) {
    std::istringstream in(content);
    std::string line;
    int n = 0;
    TextFile* cur = nullptr;
    FileEntry* entry = nullptr;
    bool program = false;
    auto finish = [&]() -> bool {
        if (entry && entry->text.empty()) { err = "riga " + std::to_string(entry->line) + ": messaggio vuoto"; return false; }
        entry = nullptr;
        return true;
    };
    while (std::getline(in, line)) {
        ++n;
        if (n == 1 && line.size() >= 3 && uint8_t(line[0]) == 0xEF && uint8_t(line[1]) == 0xBB && uint8_t(line[2]) == 0xBF)
            line.erase(0, 3);                       // UTF-8 byte order mark
        line = trimRight(line);
        if (line.rfind("@file ", 0) == 0) {
            if (!finish()) return false;
            std::istringstream ls(line.substr(6));
            TextFile f;
            if (!(ls >> f.archive >> f.lang) || f.lang < 0 || f.lang > 3) {
                err = "riga " + std::to_string(n) + ": riga @file non valida";
                return false;
            }
            program = f.archive == "MAIN.BIN";
            out.push_back(f);
            cur = &out.back();
            continue;
        }
        if (line.rfind("###", 0) == 0) {
            if (!finish()) return false;
            if (!cur) { err = "riga " + std::to_string(n) + ": manca la riga @file"; return false; }
            FileEntry e;
            e.line = n;
            std::istringstream ls(line.substr(3));
            for (std::string tok; ls >> tok;) {
                if (tok.front() == '{' && tok.back() == '}') { e.check = tok.substr(1, tok.size() - 2); continue; }
                try {
                    size_t used = 0;
                    const unsigned long v = std::stoul(tok, &used, program ? 16 : 10);
                    if (used != tok.size()) throw std::invalid_argument(tok);
                    e.ids.push_back(unsigned(v));
                } catch (const std::exception&) {
                    err = "riga " + std::to_string(n) + ": numero non valido \"" + tok + "\"";
                    return false;
                }
            }
            if (e.ids.empty()) { err = "riga " + std::to_string(n) + ": nessun numero dopo ###"; return false; }
            cur->entries.push_back(e);
            entry = &cur->entries.back();
            continue;
        }
        if (line.empty() || line[0] == '#') continue;
        if (!entry) {
            err = "riga " + std::to_string(n) + ": testo fuori da un messaggio (manca la riga ###)";
            return false;
        }
        const std::string t = entry->text.empty() ? line : trimLeft(line);
        entry->text += (entry->text.empty() ? "" : " ") + t;
    }
    return finish();
}

bool loadTexts(const std::string& path, std::vector<TextFile>& out, std::string& err) {
    std::vector<fs::path> paths;
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        for (const auto& de : fs::directory_iterator(path, ec))
            if (de.is_regular_file() && de.path().extension() == ".txt") paths.push_back(de.path());
        std::sort(paths.begin(), paths.end());
    } else {
        paths.emplace_back(path);
    }
    for (const fs::path& p : paths) {
        Bytes b;
        try { b = readFile(p.string()); } catch (const std::exception& e) { err = e.what(); return false; }
        std::vector<TextFile> files;
        if (!parseFile(std::string(b.begin(), b.end()), files, err)) {
            err = p.filename().string() + ", " + err;
            return false;
        }
        for (TextFile& f : files) { f.path = p.filename().string(); out.push_back(std::move(f)); }
    }
    return true;
}

std::string exportArchive(const GameFiles& files, const std::string& name, int lang, int refLang) {
    const Archive a = loadArchive(files, name);
    const bool story = name.rfind("txt44", 0) == 0;
    const auto ctx = contexts(a, lang, story);
    std::string title = story ? "racconto, testi dei livelli e menu iniziale"
                              : "livello " + name.substr(3, 1);
    std::ostringstream o;
    o << header(title + " (" + std::to_string(a.msgs[lang].size()) + " messaggi)", name, lang, false);
    // identical texts (with identical reference texts) are written once
    std::map<std::pair<std::string, std::string>, std::vector<size_t>> groups;
    std::vector<std::pair<std::string, std::string>> order;
    for (size_t i = 0; i < a.msgs[lang].size(); ++i) {
        const auto key = std::make_pair(a.msgs[lang][i], refLang >= 0 ? a.msgs[refLang][i] : std::string());
        auto& g = groups[key];
        if (g.empty()) order.push_back(key);
        g.push_back(i);
    }
    for (const auto& key : order) {
        const auto& ids = groups[key];
        o << "###";
        for (size_t i : ids) o << ' ' << i;
        o << '\n';
        const size_t shown = std::min<size_t>(ids.size(), 6);
        for (size_t k = 0; k < shown; ++k)
            for (const std::string& c : ctx[ids[k]]) o << "# " << (ids.size() > 1 ? std::to_string(ids[k]) + ": " : "") << c << '\n';
        if (ids.size() > shown) o << "# ... e altri " << ids.size() - shown << '\n';
        if (refLang >= 0) o << "# " << (refLang == 0 ? "EN" : "RIF") << ": " << toUtf8(key.second) << '\n';
        o << toUtf8(key.first) << "\n\n";
    }
    return o.str();
}

std::string exportProgram(const GameFiles& files, int lang) {
    const Bytes exe = rnc::unpack(files.read("MAIN.BIN"));
    std::ostringstream o;
    o << header("testi del programma", "MAIN.BIN", lang, true);
    for (const ProgramField& f : programFields(lang)) {
        if (kHunkData + f.offset + f.length > exe.size()) continue;
        std::string t(exe.begin() + long(kHunkData + f.offset), exe.begin() + long(kHunkData + f.offset + f.length));
        const size_t a = t.find_first_not_of(' '), b = t.find_last_not_of(' ');
        t = a == std::string::npos ? "" : t.substr(a, b - a + 1);
        char hex[16];
        std::snprintf(hex, sizeof hex, "%X", f.offset);
        o << "### " << hex << "\n# " << f.what << ", al massimo " << f.length << " caratteri\n"
          << toUtf8(t, false) << "\n\n";
    }
    return o.str();
}

ApplyReport apply(const std::vector<TextFile>& files, GameFiles& gameFiles) {
    ApplyReport r;
    std::map<std::string, std::vector<const TextFile*>> byArchive;
    for (const TextFile& f : files)                     // txt*.epf archives (other files are not game texts)
        if (f.archive.size() > 4 && f.archive.rfind("txt", 0) == 0 && f.archive.substr(f.archive.size() - 4) == ".epf")
            byArchive[f.archive].push_back(&f);
    for (const auto& [name, list] : byArchive) {
        Archive a;
        try { a = loadArchive(gameFiles, name); } catch (const std::exception& e) { r.errors.push_back(e.what()); continue; }
        std::map<int, std::vector<std::string>> msgs;          // language -> messages
        std::vector<size_t> longest;                           // longest original of each message (code points)
        for (size_t i = 0; i < a.msgs[0].size(); ++i) {
            size_t m = 0;
            for (int l = 0; l < 4; ++l) if (i < a.msgs[l].size()) m = std::max(m, codePoints(toUtf8(a.msgs[l][i])));
            longest.push_back(m);
        }
        std::vector<bool> isName(a.msgs[3].size(), false);
        for (const IndexEntry& e : a.entries[3]) if (isNameGroup(e.group) && e.message < isName.size()) isName[e.message] = true;
        int changed = 0;
        for (const TextFile* f : list) {
            auto& m = msgs.try_emplace(f->lang, a.msgs[f->lang]).first->second;
            for (const FileEntry& e : f->entries) {
                const std::string where = f->path + ", riga " + std::to_string(e.line);
                std::string game, bad;
                if (!fromUtf8(e.text, game, bad)) {
                    r.errors.push_back(where + ": carattere non disponibile nel gioco: " + bad);
                    continue;
                }
                bool ok = true;
                for (unsigned id : e.ids) if (id >= m.size()) { r.errors.push_back(where + ": il messaggio " + std::to_string(id) + " non esiste"); ok = false; }
                if (!ok) continue;
                if (!e.check.empty() && shortHash(a.msgs[f->lang][e.ids[0]]) != e.check) {
                    r.errors.push_back(where + ": il testo originale non corrisponde (correzione per un'altra versione?)");
                    continue;
                }
                bool tooLong = false;
                size_t limit = 0;
                for (unsigned id : e.ids) {
                    if (m[id] == game) continue;
                    m[id] = game;
                    ++changed;
                    if (id < isName.size() && isName[id] && codePoints(e.text) > longest[id]) { tooLong = true; limit = longest[id]; }
                }
                if (tooLong)
                    r.warnings.push_back(where + ": \"" + e.text + "\" e' piu' lungo del nome piu' lungo delle 4 lingue originali (" +
                                         std::to_string(limit) + " caratteri): nella vista di Lightmere va bene, sullo schermo "
                                         "originale (F8, solo con --debug) puo' finire sotto il nome del bersaglio");
            }
        }
        if (!changed) continue;
        try {
            std::vector<Bytes> raws = a.raw;
            for (const auto& [lang, m] : msgs) {
                Bytes stream, dict;
                encode(m, stream, dict);
                const Bytes index = remapIndex(a.index[lang], a.stream[lang], stream);
                if (decode(stream, dict) != m) throw FormatError("texts: check after encoding failed");
                raws[size_t(lang) * 3] = EpfArchive::stored(stream);
                raws[size_t(lang) * 3 + 1] = EpfArchive::stored(dict);
                raws[size_t(lang) * 3 + 2] = EpfArchive::stored(index);
            }
            gameFiles.replace(name, EpfArchive::build(raws));
            r.changed += changed;
        } catch (const std::exception& e) {
            r.errors.push_back(name + ": " + e.what());
        }
    }
    return r;
}

std::vector<ProgramPatch> programPatches(const std::vector<TextFile>& files, const GameFiles& gameFiles, ApplyReport& r) {
    std::vector<ProgramPatch> out;
    Bytes exe;
    for (const TextFile& f : files) {
        if (f.archive != "MAIN.BIN") continue;
        if (exe.empty()) {
            try { exe = rnc::unpack(gameFiles.read("MAIN.BIN")); } catch (const std::exception& e) { r.errors.push_back(e.what()); return out; }
        }
        for (const FileEntry& e : f.entries) {
            const std::string where = f.path + ", riga " + std::to_string(e.line);
            const auto& fields = programFields(f.lang);
            const auto it = std::find_if(fields.begin(), fields.end(), [&](const ProgramField& p) { return p.offset == e.ids[0]; });
            if (it == fields.end() || e.ids.size() != 1) { r.errors.push_back(where + ": testo del programma sconosciuto"); continue; }
            const std::string original(exe.begin() + long(kHunkData + it->offset), exe.begin() + long(kHunkData + it->offset + it->length));
            if (!e.check.empty() && shortHash(original) != e.check) {
                r.errors.push_back(where + ": il testo originale non corrisponde (correzione per un'altra versione?)");
                continue;
            }
            std::string game, bad;
            if (!fromUtf8(e.text, game, bad, false)) { r.errors.push_back(where + ": carattere non disponibile: " + bad); continue; }
            if (game.size() > it->length) {
                r.errors.push_back(where + ": troppo lungo (" + std::to_string(game.size()) + " caratteri, al massimo " +
                                   std::to_string(it->length) + ")");
                continue;
            }
            auto trim = [](const std::string& s) {
                const size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
                return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
            };
            if (trim(game) == trim(original)) continue;     // unchanged (keeps the original spacing)
            std::string field;
            if (it->centred) {
                const size_t left = (it->length - game.size()) / 2;
                field = std::string(left, ' ') + game;
            } else {
                // the digit places (+) and the percent sign keep their positions
                const size_t lead = original.find_first_not_of(' ');
                field = std::string(lead == std::string::npos ? 0 : lead, ' ') + game;
            }
            field.resize(it->length, ' ');
            bool placesKept = true;
            for (size_t k = 0; k < it->length; ++k)
                if ((original[k] == '+' || original[k] == '%' || field[k] == '+' || field[k] == '%') && original[k] != field[k]) placesKept = false;
            if (!placesKept) { r.errors.push_back(where + ": i segni + e % devono restare nelle stesse posizioni"); continue; }
            if (field == original) continue;
            out.push_back({it->offset, original, field});
            ++r.changed;
        }
    }
    return out;
}

std::string differences(const std::vector<TextFile>& files, const GameFiles& gameFiles, ApplyReport& r) {
    std::ostringstream o;
    o << "# Lightmere: corrections of the game texts. Only the corrected messages are listed, each with a check\n"
      << "# of the original text ({...}); the original texts come from the user's own copy of the game.\n";
    Bytes exe;
    for (const TextFile& f : files) {
        std::ostringstream body;
        int n = 0;
        if (f.archive == "MAIN.BIN") {
            ApplyReport tmp;
            for (const ProgramPatch& p : programPatches({f}, gameFiles, tmp)) {
                char hex[16];
                std::snprintf(hex, sizeof hex, "%X", p.offset);
                const size_t a = p.text.find_first_not_of(' '), b = p.text.find_last_not_of(' ');
                body << "### " << hex << " {" << shortHash(p.original) << "}\n" << toUtf8(p.text.substr(a, b - a + 1), false) << "\n\n";
                ++n;
            }
            r.errors.insert(r.errors.end(), tmp.errors.begin(), tmp.errors.end());
        } else if (f.archive.rfind("txt", 0) == 0) {
            Archive a;
            try { a = loadArchive(gameFiles, f.archive); } catch (const std::exception& e) { r.errors.push_back(e.what()); continue; }
            const auto& orig = a.msgs[f.lang];
            for (const FileEntry& e : f.entries) {
                std::string game, bad;
                if (!fromUtf8(e.text, game, bad)) {
                    r.errors.push_back(f.path + ", riga " + std::to_string(e.line) + ": carattere non disponibile nel gioco: " + bad);
                    continue;
                }
                std::vector<unsigned> ids;
                for (unsigned id : e.ids) if (id < orig.size() && orig[id] != game) ids.push_back(id);
                if (ids.empty()) continue;
                // one entry per original text, so that each check covers its messages
                std::map<std::string, std::vector<unsigned>> byOriginal;
                for (unsigned id : ids) byOriginal[orig[id]].push_back(id);
                for (const auto& [o2, list] : byOriginal) {
                    body << "###";
                    for (unsigned id : list) body << ' ' << id;
                    body << " {" << shortHash(o2) << "}\n" << e.text << "\n\n";
                    ++n;
                }
            }
        }
        if (n) o << "\n@file " << f.archive << ' ' << f.lang << "\n\n" << body.str();
        r.changed += n;
    }
    return o.str();
}

} // namespace dm::text
