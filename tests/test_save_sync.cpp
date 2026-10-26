// Save import/export integrity: staged NAND writes, rollback on failure and
// honest error reporting. Uses the real SaveSyncManager against temp dirs.
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
using mobile_ui::SaveIoPaths;
using mobile_ui::SaveSyncManager;
using mobile_ui::savefs::validateSaveDir;

namespace {

int failures = 0;

void check(const char* name, bool ok, const std::string& detail = "") {
    printf("%-56s %s%s%s\n", name, ok ? "OK" : "FAIL",
           detail.empty() ? "" : " - ", detail.c_str());
    if (!ok) ++failures;
}

bool writeFile(const fs::path& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return out.good();
}

std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

const char* const kTitles[] = {"524d4750", "524d4745", "524d474a", "524d474b"};
const char* const kNames[] = {"GameData.bin", "save.bin"};

struct Fixture {
    fs::path root;
    fs::path nand;
    fs::path download;
    fs::path dolphin;
    fs::path mirror;
    SaveIoPaths paths;
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

void testImportWritesEveryCopy() {
    Fixture fixture;
    const std::vector<uint8_t> sample = nativeSave(0x5a);
    fs::create_directories(fixture.download);
    writeFile(fixture.download / "GameData.bin", sample);

    std::string message;
    bool ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("import succeeds", ok, message);
    for (const char* id : kTitles) {
        for (const char* name : kNames) {
            check((std::string("imported ") + id + "/" + name).c_str(),
                  readFile(fixture.titleFile(id, name)) == sample);
        }
    }
    check("imported NAND validates", validateSaveDir(fixture.nand.generic_string()));
    check("import mirrors externally", readFile(fixture.mirror / "GameData.bin") == sample);
    check("import staging removed", !fs::exists(fixture.nand / "import_staging"));
}

void testImportRollsBackOnPromotionFailure() {
    Fixture fixture;
    const std::vector<uint8_t> previous = nativeSave(0x11);
    fs::create_directories(fixture.titleFile("524d4750", "GameData.bin").parent_path());
    writeFile(fixture.titleFile("524d4750", "GameData.bin"), previous);
    // A regular file where the next title's directory must be created makes
    // promotion fail after the first title was already replaced.
    writeFile(fixture.nand / "title" / "00010000" / "524d4745", nativeSave(0x22));

    fs::create_directories(fixture.download);
    writeFile(fixture.download / "GameData.bin", nativeSave(0x5a));
    std::string message;
    bool ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("import refuses blocked NAND", !ok, message);
    check("rollback restores previous save",
          readFile(fixture.titleFile("524d4750", "GameData.bin")) == previous);
    check("rollback removes partially promoted file",
          !fs::exists(fixture.titleFile("524d4750", "save.bin")));
    check("rollback leaves no staging", !fs::exists(fixture.nand / "import_staging"));
    check("rollback leaves no backups", !fixture.hasLeftover(".import_rollback"));
    check("failed import does not mirror", !fs::exists(fixture.mirror / "GameData.bin"));
}

void testUnrecoverableImportIsRefused() {
    Fixture fixture;
    const std::string dest = fixture.titleFile("524d4750", "GameData.bin").generic_string();
    fs::create_directories(dest);
    fs::create_directories(dest + ".import_rollback");
    writeFile(fs::path(dest + ".import_rollback") / "stale", nativeSave(0x11));
    fs::create_directories(fixture.download); writeFile(fixture.download / "GameData.bin", nativeSave(0x5a));
    std::string message; bool ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("unrecoverable import is refused", !ok, message);
    check("unrecoverable import explains recovery", message.find("recover") != std::string::npos, message);
    check("unrecoverable import keeps rollback data", fs::exists(fs::path(dest + ".import_rollback") / "stale"));
}

#ifndef _WIN32
void testBlockedStagingLeavesNandUntouched() {
    Fixture fixture;
    fs::create_directories(fixture.download); writeFile(fixture.download / "GameData.bin", nativeSave(0x5a));
    const fs::path blocked = fixture.nand / "import_staging" / "blocked";
    fs::create_directories(blocked); writeFile(blocked / "x", nativeSave(1));
    fs::permissions(blocked, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
    std::string message; bool ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("blocked staging refuses import and reports unchanged NAND",
          !ok && message.find("not changed") != std::string::npos, message);
    check("blocked staging writes no saves", !fs::exists(fixture.nand / "title"));
    fs::permissions(blocked, fs::perms::owner_all, fs::perm_options::replace);
}
#endif

void testImportRejectsOversizedFile() {
    Fixture fixture;
    fs::create_directories(fixture.download);
    writeFile(fixture.download / "GameData.bin", std::vector<uint8_t>(0x10001, 0));
    std::string message;
    bool ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("oversized import rejected", !ok);
    check("oversized import explains size", message.find("too large") != std::string::npos, message);
    check("oversized import writes nothing", !fs::exists(fixture.nand / "title"));
}

void testImportConvertsDolphinSave() {
    Fixture fixture;
    fs::create_directories(fixture.download);
    writeFile(fixture.download / "GameData_Dolphin.bin", dolphinSave());
    std::string message;
    bool ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("Dolphin import succeeds", ok, message);
    const std::vector<uint8_t> stored = readFile(fixture.titleFile("524d4750", "GameData.bin"));
    check("Dolphin import converts to native layout",
          stored.size() == 0x400 && stored[4] == 2 && stored[7] == 0 && stored[8] == 19);
    check("Dolphin import reports conversion", message.find("Dolphin") != std::string::npos, message);
}

void testExportWritesVerifiedCopies() {
    Fixture fixture;
    const std::vector<uint8_t> sample = nativeSave(0x77);
    fs::create_directories(fixture.titleFile("524d4750", "GameData.bin").parent_path());
    writeFile(fixture.titleFile("524d4750", "GameData.bin"), sample);

    std::string message;
    bool ok = SaveSyncManager::exportSave(fixture.savePath, message, fixture.paths);
    check("export succeeds", ok, message);
    check("export writes GalaxyAndroid_save.bin",
          readFile(fixture.download / "GalaxyAndroid_save.bin") == sample);
    check("export writes GameData.bin", readFile(fixture.download / "GameData.bin") == sample);

    std::vector<uint8_t> converted;
    mobile_ui::DolphinSaveConverter::convertNativeToDolphin(
        sample.data(), sample.size(), converted);
    check("export writes Dolphin save",
          readFile(fixture.download / "GameData_Dolphin.bin") == converted);
}

void testExportReportsWriteFailure() {
    Fixture fixture;
    const std::vector<uint8_t> sample = nativeSave(0x77);
    fs::create_directories(fixture.titleFile("524d4750", "GameData.bin").parent_path());
    writeFile(fixture.titleFile("524d4750", "GameData.bin"), sample);
    // A file where the download directory belongs forces every write to fail.
    fixture.paths.downloadDir = (fixture.root / "blocked").generic_string();
    writeFile(fixture.root / "blocked", sample);

    std::string message;
    bool ok = SaveSyncManager::exportSave(fixture.savePath, message, fixture.paths);
    check("export reports unwritable destination", !ok, message);
    check("export failure is explicit", message.find("failed") != std::string::npos ||
                                             message.find("Could not") != std::string::npos,
          message);
}

void testImportRecoversInterruptedPromotion() {
    Fixture fixture;
    const std::vector<uint8_t> previous = nativeSave(0x31);
    fs::create_directories(fixture.titleFile("524d4750", "GameData.bin").parent_path());
    writeFile(fixture.titleFile("524d4750", "GameData.bin"), previous);
    fs::rename(fixture.titleFile("524d4750", "GameData.bin"),
               fixture.titleFile("524d4750", "GameData.bin.import_rollback"));
    fs::create_directories(fixture.nand / "import_staging" / "title");
    writeFile(fixture.nand / "import_staging" / "title" / "partial.bin", nativeSave(0x99));

    std::string message;
    bool ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("interrupted import reports no source", !ok, message);
    check("interrupted import restores previous save",
          readFile(fixture.titleFile("524d4750", "GameData.bin")) == previous);
    check("recovery leaves no rollback files", !fixture.hasLeftover(".import_rollback"));
    check("recovery removes interrupted staging", !fs::exists(fixture.nand / "import_staging"));
}

void testExportRecoversInterruptedPromotion() {
    Fixture fixture;
    const std::vector<uint8_t> current = nativeSave(0x42);
    fs::create_directories(fixture.titleFile("524d4750", "GameData.bin").parent_path());
    writeFile(fixture.titleFile("524d4750", "GameData.bin"), current);
    writeFile(fixture.titleFile("524d4750", "GameData.bin.import_rollback"),
              nativeSave(0x11));

    std::string message;
    bool ok = SaveSyncManager::exportSave(fixture.savePath, message, fixture.paths);
    check("export after interrupted promotion succeeds", ok, message);
    check("export keeps the promoted save",
          readFile(fixture.download / "GameData.bin") == current);
    check("export clears stale rollback files", !fixture.hasLeftover(".import_rollback"));
}

void testImportRejectsCorruptChecksum() {
    Fixture fixture;
    fs::create_directories(fixture.download);
    std::vector<uint8_t> corrupt = nativeSave(0x5a);
    corrupt[0x200] ^= 0xFF;
    writeFile(fixture.download / "GameData.bin", corrupt);
    std::string message; bool ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("corrupt native checksum rejected", !ok, message);
    check("corrupt native import writes nothing", !fs::exists(fixture.nand / "title"));

    corrupt = dolphinSave();
    corrupt[0x200] ^= 0xFF;
    writeFile(fixture.download / "GameData.bin", corrupt);
    message.clear(); ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("corrupt Dolphin checksum rejected", !ok, message);
    check("corrupt Dolphin import writes nothing", !fs::exists(fixture.nand / "title"));
}

}  // namespace

int main() {
    testImportWritesEveryCopy();
    testImportRollsBackOnPromotionFailure();
    testUnrecoverableImportIsRefused();
#ifndef _WIN32
    testBlockedStagingLeavesNandUntouched();
#endif
    testImportRecoversInterruptedPromotion();
    testExportRecoversInterruptedPromotion();
    testImportRejectsOversizedFile();
    testImportConvertsDolphinSave();
    testImportRejectsCorruptChecksum();
    testExportWritesVerifiedCopies();
    testExportReportsWriteFailure();

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
