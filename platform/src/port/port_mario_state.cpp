#include <cstdio>
#include <cstring>
#include <string>
#include "port/heap_routing.h"
#include "port/port.h"

extern "C" const char* port_nand_get_root(void);

namespace {

struct PortMarioStateHeader {
    uint32_t magic;
    char stageName[48];
    float pos[3];
    float vel[3];
    float front[3];
    int32_t health;
};

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
    if (!stage || !pos || !vel || !front) return;
    std::string path = getMarioStatePath();
    if (path.empty()) return;

    PortMarioStateHeader hdr{};
    hdr.magic = 0x4D41524F;
    strncpy(hdr.stageName, stage, sizeof(hdr.stageName) - 1);
    hdr.pos[0] = pos[0]; hdr.pos[1] = pos[1]; hdr.pos[2] = pos[2];
    hdr.vel[0] = vel[0]; hdr.vel[1] = vel[1]; hdr.vel[2] = vel[2];
    hdr.front[0] = front[0]; hdr.front[1] = front[1]; hdr.front[2] = front[2];
    hdr.health = health;

    FILE* f = fopen(path.c_str(), "wb");
    if (f) {
        fwrite(&hdr, 1, sizeof(hdr), f);
        fclose(f);
    }
}

int port_load_mario_state(char* stageOut, size_t maxLen, float posOut[3], float velOut[3],
                          float frontOut[3], int* healthOut) {
    std::string path = getMarioStatePath();
    if (path.empty()) return 0;

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return 0;

    PortMarioStateHeader hdr{};
    size_t rd = fread(&hdr, 1, sizeof(hdr), f);
    fclose(f);

    if (rd != sizeof(hdr) || hdr.magic != 0x4D41524F) return 0;

    if (stageOut && maxLen > 0) {
        strncpy(stageOut, hdr.stageName, maxLen - 1);
        stageOut[maxLen - 1] = '\0';
    }
    if (posOut) {
        posOut[0] = hdr.pos[0]; posOut[1] = hdr.pos[1]; posOut[2] = hdr.pos[2];
    }
    if (velOut) {
        velOut[0] = hdr.vel[0]; velOut[1] = hdr.vel[1]; velOut[2] = hdr.vel[2];
    }
    if (frontOut) {
        frontOut[0] = hdr.front[0]; frontOut[1] = hdr.front[1]; frontOut[2] = hdr.front[2];
    }
    if (healthOut) *healthOut = hdr.health;
    return 1;
}

void port_clear_mario_state(void) {
    std::string path = getMarioStatePath();
    if (!path.empty()) {
        remove(path.c_str());
    }
}

}  // extern "C"
