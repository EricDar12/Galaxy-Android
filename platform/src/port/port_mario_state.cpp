#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "mobile/ui/SaveStorage.h"
#include "port/heap_routing.h"
#include "port/port.h"

extern "C" const char* port_nand_get_root(void);

namespace {

constexpr uint32_t kMarioStateMagic = 0x4D415232; // 'MAR2' (Version 2)
constexpr uint32_t kMarioStateLegacyMagic = 0x4D41524F; // 'MARO' (Version 1)
constexpr float kMaxCoord = 1.0e7f;

struct PortMarioStateHeader {
    uint32_t magic;
    uint32_t version;
    char stageName[48];
    float pos[3];
    float vel[3];
    float front[3];
    int32_t health;
    uint32_t checksum;
};

struct PortMarioStateLegacy {
    uint32_t magic;
    char stageName[48];
    float pos[3];
    float vel[3];
    float front[3];
    int32_t health;
};

uint32_t calcCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int)(crc & 1)));
        }
    }
    return ~crc;
}

bool isValidFloat(float v, float maxVal) {
    return !std::isnan(v) && !std::isinf(v) && std::fabs(v) <= maxVal;
}

std::string getMarioStatePath() {
    PortHostAllocScope scope;
    const char* root = port_nand_get_root();
    if (!root || !root[0]) return "";
    return std::string(root) + "/title/00010000/524d4750/data/mario_state.bin";
}

}  // namespace

extern "C" {

void port_save_mario_state(const char* stage, const float pos[3], const float vel[3],
                           const float front[3], int health) {
    if (!stage || !stage[0] || !pos || !vel || !front) return;

    for (int i = 0; i < 3; ++i) {
        if (!isValidFloat(pos[i], kMaxCoord)) return;
        if (!isValidFloat(vel[i], 10000.0f)) return;
        if (!isValidFloat(front[i], 10.0f)) return;
    }
    if (health < 0 || health > 8) return;

    PortMarioStateHeader hdr{};
    hdr.magic = kMarioStateMagic;
    hdr.version = 2;
    strncpy(hdr.stageName, stage, sizeof(hdr.stageName) - 1);
    hdr.stageName[sizeof(hdr.stageName) - 1] = '\0';
    for (int i = 0; i < 3; ++i) {
        hdr.pos[i] = pos[i];
        hdr.vel[i] = vel[i];
        hdr.front[i] = front[i];
    }
    hdr.health = health;

    size_t payloadLen = offsetof(PortMarioStateHeader, checksum);
    hdr.checksum = calcCrc32(reinterpret_cast<const uint8_t*>(&hdr), payloadLen);

    // Runs on a game thread, so the helper's temporary strings must not land in
    // the JKR heap that is torn down with the scene.
    PortHostAllocScope scope;
    std::string path = getMarioStatePath();
    if (path.empty()) return;
    mobile_ui::savefs::writeFileAtomic(path, &hdr, sizeof(hdr));
}

int port_load_mario_state(char* stageOut, size_t maxLen, float posOut[3], float velOut[3],
                          float frontOut[3], int* healthOut) {
    std::string path = getMarioStatePath();
    if (path.empty()) return 0;

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return 0;

    uint8_t buf[sizeof(PortMarioStateHeader)]{};
    size_t rd = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    if (rd >= sizeof(PortMarioStateHeader)) {
        const auto* hdr = reinterpret_cast<const PortMarioStateHeader*>(buf);
        if (hdr->magic == kMarioStateMagic && hdr->version == 2) {
            size_t payloadLen = offsetof(PortMarioStateHeader, checksum);
            uint32_t expected = calcCrc32(buf, payloadLen);
            if (expected != hdr->checksum) return 0;

            for (int i = 0; i < 3; ++i) {
                if (!isValidFloat(hdr->pos[i], kMaxCoord)) return 0;
                if (!isValidFloat(hdr->vel[i], 10000.0f)) return 0;
                if (!isValidFloat(hdr->front[i], 10.0f)) return 0;
            }
            if (hdr->health < 0 || hdr->health > 8) return 0;

            if (stageOut && maxLen > 0) {
                strncpy(stageOut, hdr->stageName, maxLen - 1);
                stageOut[maxLen - 1] = '\0';
            }
            if (posOut) for (int i = 0; i < 3; ++i) posOut[i] = hdr->pos[i];
            if (velOut) for (int i = 0; i < 3; ++i) velOut[i] = hdr->vel[i];
            if (frontOut) for (int i = 0; i < 3; ++i) frontOut[i] = hdr->front[i];
            if (healthOut) *healthOut = hdr->health;
            return 1;
        }
    }

    if (rd == sizeof(PortMarioStateLegacy)) {
        const auto* legacy = reinterpret_cast<const PortMarioStateLegacy*>(buf);
        if (legacy->magic == kMarioStateLegacyMagic) {
            for (int i = 0; i < 3; ++i) {
                if (!isValidFloat(legacy->pos[i], kMaxCoord)) return 0;
                if (!isValidFloat(legacy->vel[i], 10000.0f)) return 0;
                if (!isValidFloat(legacy->front[i], 10.0f)) return 0;
            }
            if (legacy->health < 0 || legacy->health > 8) return 0;

            if (stageOut && maxLen > 0) {
                strncpy(stageOut, legacy->stageName, maxLen - 1);
                stageOut[maxLen - 1] = '\0';
            }
            if (posOut) for (int i = 0; i < 3; ++i) posOut[i] = legacy->pos[i];
            if (velOut) for (int i = 0; i < 3; ++i) velOut[i] = legacy->vel[i];
            if (frontOut) for (int i = 0; i < 3; ++i) frontOut[i] = legacy->front[i];
            if (healthOut) *healthOut = legacy->health;
            return 1;
        }
    }

    return 0;
}

void port_clear_mario_state(void) {
    std::string path = getMarioStatePath();
    if (!path.empty()) {
        remove(path.c_str());
    }
}

}  // extern "C"
