// Save import integrity: staged NAND writes, rollback on failure and honest
// error reporting. Uses the real SaveSyncManager against temp dirs. The export
// and validation half runs from test_save_sync_export.cpp in the same binary.
#include "save_sync_harness.h"

using mobile_ui::SaveSyncManager;
using mobile_ui::savefs::validateSaveDir;

namespace {

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

// The quick-save position snapshot belongs to the save it was taken from; an
// import must not leave it behind to reposition the newly imported game.
void testImportDropsStalePositionSnapshot() {
    Fixture fixture;
    fs::create_directories(fixture.download);
    writeFile(fixture.download / "GameData.bin", nativeSave(0x5a));
    const fs::path snapshot =
        fixture.titleFile("524d4750", "GameData.bin").parent_path() / "mario_state.bin";
    fs::create_directories(snapshot.parent_path());
    writeFile(snapshot, std::vector<uint8_t>(100, 0x7b));

    std::string message;
    bool ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("import over a stale snapshot succeeds", ok, message);
    check("import drops the stale snapshot", !fs::exists(snapshot));

    // A refused import changes nothing, so the live snapshot has to survive.
    writeFile(snapshot, std::vector<uint8_t>(100, 0x7b));
    std::vector<uint8_t> corrupt = nativeSave(0x5a);
    corrupt[0x200] ^= 0xFF;
    writeFile(fixture.download / "GameData.bin", corrupt);
    message.clear();
    ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("refused import keeps the live snapshot", !ok && fs::exists(snapshot), message);
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
    testImportRejectsOversizedFile();
    testImportConvertsDolphinSave();
    testImportRejectsCorruptChecksum();
    testImportDropsStalePositionSnapshot();
    runSaveSyncExportChecks();

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
