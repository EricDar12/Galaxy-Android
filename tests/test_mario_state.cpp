// The quick-save position snapshot is written from a game thread and read back
// after a scene reload, so the bytes on disk are the only thing that survives.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define TEST_GETPID _getpid
#else
#include <unistd.h>
#define TEST_GETPID getpid
#endif

#include "port/port.h"

namespace fs = std::filesystem;

namespace {

// offsetof(PortMarioStateHeader, checksum) + the stored checksum itself.
constexpr size_t kRecordSize = 100;
constexpr size_t kLegacyRecordSize = 92;

std::string gNandRoot;
int failures = 0;

void check(const char* name, bool ok, const std::string& detail = "") {
    printf("%-56s %s%s%s\n", name, ok ? "OK" : "FAIL", detail.empty() ? "" : " - ",
           detail.c_str());
    if (!ok) ++failures;
}

const fs::path& statePath() {
    static const fs::path path =
        fs::path(gNandRoot) / "title" / "00010000" / "524d4750" / "data" / "mario_state.bin";
    return path;
}

std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

void setUp() {
    gNandRoot = (fs::temp_directory_path() /
                 ("galaxy_mario_state_" + std::to_string(TEST_GETPID()))).generic_string();
    std::error_code ec;
    fs::remove_all(gNandRoot, ec);
    fs::create_directories(statePath().parent_path());
}

void tearDown() {
    std::error_code ec;
    fs::remove_all(gNandRoot, ec);
}

void testRoundTrip() {
    setUp();
    const float pos[3] = {1.5f, -2.25f, 300.5f};
    const float vel[3] = {0.0f, 3.5f, -12.25f};
    const float front[3] = {0.0f, 0.0f, 1.0f};
    port_save_mario_state("PeachCastleGardenGalaxy", pos, vel, front, 5);

    check("snapshot written", fs::exists(statePath()));
    check("snapshot leaves no temp file", !fs::exists(statePath().string() + ".tmp"));
    check("snapshot is one fixed-size record", fs::file_size(statePath()) == kRecordSize);

    char stage[64] = {0};
    float outPos[3]{}, outVel[3]{}, outFront[3]{};
    int health = 0;
    int ok = port_load_mario_state(stage, sizeof(stage), outPos, outVel, outFront, &health);
    check("snapshot loads", ok == 1);
    check("stage name round-trips", !strcmp(stage, "PeachCastleGardenGalaxy"), stage);
    check("health round-trips", health == 5);
    bool vectorsMatch = true;
    for (int i = 0; i < 3; ++i) {
        vectorsMatch = vectorsMatch && outPos[i] == pos[i] && outVel[i] == vel[i] &&
                       outFront[i] == front[i];
    }
    check("position, velocity and facing round-trip", vectorsMatch);

    // A second save must replace the first wholesale, not append or half-write.
    const float moved[3] = {9.0f, 9.0f, 9.0f};
    port_save_mario_state("DomeGalaxy", moved, vel, front, 2);
    ok = port_load_mario_state(stage, sizeof(stage), outPos, outVel, outFront, &health);
    check("rewrite replaces the snapshot",
          ok == 1 && !strcmp(stage, "DomeGalaxy") && health == 2 && outPos[0] == 9.0f);
    check("rewrite keeps one fixed-size record", fs::file_size(statePath()) == kRecordSize);
    tearDown();
}

void testRejectsInvalidState() {
    setUp();
    const float good[3] = {1.0f, 2.0f, 3.0f};
    const float nanPos[3] = {NAN, 2.0f, 3.0f};
    const float hugePos[3] = {1.0e9f, 2.0f, 3.0f};
    const float nanVel[3] = {1.0f, NAN, 3.0f};
    const float wildFront[3] = {0.0f, 20.0f, 0.0f};

    port_save_mario_state("Stage", nanPos, good, good, 3);
    port_save_mario_state("Stage", hugePos, good, good, 3);
    port_save_mario_state("Stage", good, nanVel, good, 3);
    port_save_mario_state("Stage", good, good, wildFront, 3);
    port_save_mario_state("Stage", good, good, good, 9);
    port_save_mario_state("Stage", good, good, good, -1);
    port_save_mario_state("", good, good, good, 3);
    port_save_mario_state(nullptr, good, good, good, 3);
    check("out-of-range state is not written", !fs::exists(statePath()));

    char stage[64]{};
    float out[3]{}, outVel[3]{}, outFront[3]{};
    int health = 0;
    check("missing snapshot does not load",
          port_load_mario_state(stage, sizeof(stage), out, outVel, outFront, &health) == 0);
    tearDown();
}

void testTornSnapshotIsRefused() {
    setUp();
    const float good[3] = {1.0f, 2.0f, 3.0f};
    port_save_mario_state("Stage", good, good, good, 3);
    std::vector<uint8_t> bytes = readFile(statePath());
    check("snapshot is one fixed-size record", bytes.size() == kRecordSize);

    char stage[64]{};
    float outPos[3]{}, outVel[3]{}, outFront[3]{};
    int health = 0;

    std::vector<uint8_t> corrupt = bytes;
    corrupt[corrupt.size() / 2] ^= 0xFF;
    std::ofstream out(statePath(), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(corrupt.data()),
              static_cast<std::streamsize>(corrupt.size()));
    out.close();
    check("checksum mismatch refused",
          port_load_mario_state(stage, sizeof(stage), outPos, outVel, outFront, &health) == 0);

    std::vector<uint8_t> torn(bytes.begin(), bytes.begin() + 20);
    out.open(statePath(), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(torn.data()),
              static_cast<std::streamsize>(torn.size()));
    out.close();
    check("truncated snapshot refused",
          port_load_mario_state(stage, sizeof(stage), outPos, outVel, outFront, &health) == 0);

    // Clearing is how stage transitions drop a snapshot; it must actually remove
    // the file rather than leave a record the next scene can read.
    out.open(statePath(), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    port_clear_mario_state();
    check("clear removes the snapshot", !fs::exists(statePath()));
    tearDown();
}

// Mirrors the writer's version-1 record so the legacy reader path stays honest.
struct LegacyMarioState {
    uint32_t magic;
    char stageName[48];
    float pos[3];
    float vel[3];
    float front[3];
    int32_t health;
};
static_assert(sizeof(LegacyMarioState) == kLegacyRecordSize,
              "legacy record layout must match the reader's fallback");

void testLegacySnapshotStillLoads() {
    setUp();
    LegacyMarioState rec{};
    rec.magic = 0x4D41524F;  // 'MARO'
    strcpy(rec.stageName, "OldSaveStage");
    rec.pos[0] = 4.0f;
    rec.vel[2] = -1.0f;
    rec.front[2] = 1.0f;
    rec.health = 7;
    std::ofstream out(statePath(), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
    out.close();

    char stage[64]{};
    float outPos[3]{}, outVel[3]{}, outFront[3]{};
    int health = 0;
    int ok = port_load_mario_state(stage, sizeof(stage), outPos, outVel, outFront, &health);
    check("legacy snapshot loads",
          ok == 1 && !strcmp(stage, "OldSaveStage") && outPos[0] == 4.0f && health == 7);

    LegacyMarioState bad = rec;
    bad.health = 99;
    out.open(statePath(), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(&bad), sizeof(bad));
    out.close();
    check("legacy snapshot still range-checked",
          port_load_mario_state(stage, sizeof(stage), outPos, outVel, outFront, &health) == 0);
    tearDown();
}

}  // namespace

extern "C" const char* port_nand_get_root(void) { return gNandRoot.c_str(); }
extern "C" void port_host_alloc_scope_enter(void) {}
extern "C" void port_host_alloc_scope_leave(void) {}

int main() {
    testRoundTrip();
    testRejectsInvalidState();
    testTornSnapshotIsRefused();
    testLegacySnapshotStillLoads();

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
