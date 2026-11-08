// RadiantFighter: the four Shadow Fighter disk images (WHDLoad Disk.1-4 or the ADF files, 901,120 bytes each).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rf {

constexpr uint32_t kDiskSize = 901120, kTrack = 0x1600;      // logical track of the game loader

class Disks {
public:
    static Disks& instance();
    // loads Disk.1..Disk.4 (or *Disk N of 4*.adf) from a folder; err tells what is missing
    bool load(const std::string& folder, std::string& err);
    // tries each folder (and its ShadowFighterAGA / data subfolders); msg: what was found or what is missing
    bool locate(const std::vector<std::string>& folders, std::string& msg);
    const std::string& folder() const { return folder_; }
    // true if every image has a known SHA-256 (WHDLoad v1.0 install or the PSG ADF set)
    bool known() const { return known_; }
    bool ready() const { return ready_; }
    // copies bytes [offset, offset+size) of disk n (1-4); false if outside the image
    bool read(int disk, uint32_t offset, uint32_t size, uint8_t* dst) const;
    const std::vector<uint8_t>& image(int disk) const { return img_[size_t(disk - 1)]; }
private:
    std::vector<uint8_t> img_[4];
    bool ready_ = false, known_ = false;
    std::string folder_;
};

} // namespace rf
