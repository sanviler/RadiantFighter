// SPDX-License-Identifier: GPL-3.0-or-later
#include "rf/disks.h"
#include "platform/sha256.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace rf {

Disks& Disks::instance() { static Disks d; return d; }

static bool readFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return out.size() == kDiskSize;
}

bool Disks::load(const std::string& folder, std::string& err) {
    ready_ = false;
    for (int n = 1; n <= 4; ++n) {
        bool ok = readFile(fs::path(folder) / ("Disk." + std::to_string(n)), img_[n - 1]);
        if (!ok) {
            std::error_code ec;
            for (auto& e : fs::directory_iterator(folder, ec)) {
                std::string name = e.path().filename().string(), low = name;
                std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                if (low.size() > 4 && low.substr(low.size() - 4) == ".adf" && low.find("disk " + std::to_string(n) + " of 4") != std::string::npos
                    && readFile(e.path(), img_[n - 1])) { ok = true; break; }
            }
        }
        if (!ok) { err = "Shadow Fighter disk " + std::to_string(n) + " not found in " + folder; return false; }
    }
    static const char* kKnown[] = {
        "844bfcf2543fceef291eb2dba08932201f6f36b224785e9b2e9cac7a1538f401",   // Disk.1 (WHDLoad v1.0)
        "58f5d2e06ebfbd69202cb983c3e1ee5d4171bf51f69c28dd54c707b582787a20",   // disk 1 ADF (PSG)
        "646863b1fb17eee797dbbcb6f198d8f52a8cf079ad7d6bfc9f748899a2e5de3c",   // disk 2
        "536bd589b30d808ce550c26443a829190c7bf3e1f00554208b79295cf0b60522",   // disk 3
        "d0e4df72e8022668a455ab4a71414dff09863d94572dbdfb2a32b204eea4c4af"};  // disk 4
    known_ = true;
    for (int n = 0; n < 4; ++n) {
        const std::string d = dm::sha256Hex(img_[n].data(), img_[n].size());
        bool ok = false;
        for (const char* k : kKnown) ok |= d == k;
        known_ &= ok;
    }
    folder_ = folder;
    ready_ = true;
    return true;
}

bool Disks::locate(const std::vector<std::string>& folders, std::string& msg) {
    std::string err;
    for (const auto& f : folders)
        for (const char* sub : {"", "ShadowFighterAGA", "ShadowFighter", "data"}) {
            const std::string p = std::string(sub).empty() ? f : (fs::path(f) / sub).string();
            if (load(p, err)) {
                msg = known_ ? "Shadow Fighter disks found" : "Shadow Fighter disks found (unknown version: they may not work)";
                return true;
            }
        }
    msg = "Shadow Fighter (AGA) disks not found: Disk.1 ... Disk.4 of the WHDLoad install (or the four ADF files) are needed";
    return false;
}

bool Disks::read(int disk, uint32_t offset, uint32_t size, uint8_t* dst) const {
    if (disk < 1 || disk > 4 || !ready_) return false;
    const auto& d = img_[disk - 1];
    if (offset >= d.size()) return false;
    const uint32_t n = std::min<uint32_t>(size, uint32_t(d.size()) - offset);
    std::memcpy(dst, d.data() + offset, n);
    if (n < size) std::memset(dst + n, 0, size - n);
    return n == size;
}

} // namespace rf
