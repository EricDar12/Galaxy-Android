// Synthetic NAND saves with valid checksums for save import/export tests.
#pragma once

#include <stdint.h>

#include <cstring>
#include <vector>

#include "DolphinSaveConverter.h"

inline std::vector<uint8_t> nativeSave(uint8_t tag) {
    std::vector<uint8_t> data(0x400, 0);
    data[4] = 2;   // native layout: little-endian version 2
    data[8] = 19;  // 19 files
    data[12] = 0x00;
    data[13] = 0x04;
    data[0x100] = tag;
    uint32_t sum = mobile_ui::DolphinSaveConverter::calcChecksum(data.data(), data.size(), false);
    memcpy(data.data(), &sum, 4);
    return data;
}

inline std::vector<uint8_t> dolphinSave() {
    std::vector<uint8_t> data(0x400, 0);
    data[7] = 2;   // big-endian version 2
    data[11] = 19; // 19 file records
    data[14] = 4;  // declared file size 0x400
    uint32_t sum = mobile_ui::DolphinSaveConverter::calcChecksum(data.data(), data.size(), true);
    data[0] = (uint8_t)(sum >> 24);
    data[1] = (uint8_t)(sum >> 16);
    data[2] = (uint8_t)(sum >> 8);
    data[3] = (uint8_t)sum;
    return data;
}
