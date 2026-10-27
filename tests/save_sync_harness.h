// Shared harness for the save-sync tests. The import path and the export path
// live in separate translation units (the repository caps every maintained file
// at 300 lines) but are linked into one executable, so the failure counter and
// the NAND fixture they both build are here rather than duplicated.
#pragma once

#include "DolphinSaveConverter.h"
#include "SaveStorage.h"
#include "SaveSyncManager.h"
#include "save_fixtures.h"

#include <stdio.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define TEST_GETPID _getpid
#else
#include <unistd.h>
#define TEST_GETPID getpid
#endif

namespace fs = std::filesystem;

inline int failures = 0;

inline void check(const char* name, bool ok, const std::string& detail = "") {
    printf("%-56s %s%s%s\n", name, ok ? "OK" : "FAIL",
           detail.empty() ? "" : " - ", detail.c_str());
    if (!ok) ++failures;
}

inline bool writeFile(const fs::path& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return out.good();
}

inline std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

inline const char* const kTitles[] = {"524d4750", "524d4745", "524d474a", "524d474b"};
inline const char* const kNames[] = {"GameData.bin", "save.bin"};

struct Fixture {
    fs::path root;
    fs::path nand;
    fs::path download;
    fs::path dolphin;
    fs::path mirror;
    mobile_ui::SaveIoPaths paths;
    std::string savePath;

    Fixture() {
        root = fs::temp_directory_path() /
               ("galaxy_save_sync_" + std::to_string(TEST_GETPID()) + "_" +
                std::to_string(static_cast<unsigned long long>(::time(nullptr))));
        fs::remove_all(root);
        nand = root / "nand";
        download = root / "Download";
        dolphin = root / "dolphin";
        mirror = root / "mirror";
        fs::create_directories(nand);
        paths.downloadDir = download.generic_string();
        paths.dolphinDir = dolphin.generic_string();
        paths.mirrorDir = mirror.generic_string();
        savePath = (nand / "touch_settings.ini").generic_string();
    }

    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }

    fs::path titleFile(const char* id, const char* name) const {
        return nand / "title" / "00010000" / id / "data" / name;
    }

    bool hasLeftover(const std::string& marker) const {
        for (const auto& entry : fs::recursive_directory_iterator(nand)) {
            if (entry.path().filename().string().find(marker) != std::string::npos) return true;
        }
        return false;
    }
};

// Defined in test_save_sync_export.cpp and called from main() alongside the
// import checks in this executable.
void runSaveSyncExportChecks();
