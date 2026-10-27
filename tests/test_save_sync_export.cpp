// Save export and validation integrity: the verified-copy behaviour the device
// relies on, and the gate that refuses a slot the game could not load. Runs in
// the same binary as test_save_sync.cpp, which holds the import side.
#include "save_sync_harness.h"

using mobile_ui::SaveSyncManager;
using mobile_ui::savefs::validateSaveDir;

namespace {

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

// Slot and mirror directories are validated before they are allowed to replace
// live NAND data, so the gate has to reject anything the game itself would
// refuse on load -- not merely "a big file with GameData.bin in its name".
void testValidateSaveDirRequiresALoadableSave() {
    Fixture fixture;
    const fs::path slot = fixture.mirror / "slot1";
    const std::string slotDir = slot.generic_string();

    fs::create_directories(slot);
    check("empty slot rejected", !validateSaveDir(slotDir));

    writeFile(slot / "GameData.bin", std::vector<uint8_t>(0x400, 0));
    check("all-zero blob rejected", !validateSaveDir(slotDir));
    mobile_ui::savefs::removeDirRecursive(slotDir);

    fs::create_directories(slot);
    const std::vector<uint8_t> valid = nativeSave(0x5a);
    std::vector<uint8_t> truncated(valid.begin(), valid.begin() + 0x200);
    writeFile(slot / "GameData.bin", truncated);
    check("truncated save rejected", !validateSaveDir(slotDir));
    // The load path copies the slot into staging first; both gates must agree
    // that this slot cannot be restored.
    const std::string staging = (fixture.mirror / "staging").generic_string();
    mobile_ui::savefs::removeDirRecursive(staging);
    check("truncated save survives staging but fails it too",
          mobile_ui::savefs::copyDirVerified(slotDir, staging) && !validateSaveDir(staging));
    mobile_ui::savefs::removeDirRecursive(slotDir);
    mobile_ui::savefs::removeDirRecursive(staging);

    fs::create_directories(slot);
    std::vector<uint8_t> torn = valid;
    torn[0x200] ^= 0xFF;
    writeFile(slot / "GameData.bin", torn);
    check("torn save rejected", !validateSaveDir(slotDir));
    mobile_ui::savefs::removeDirRecursive(slotDir);

    fs::create_directories(slot);
    writeFile(slot / "GameData.bin", dolphinSave());
    check("big-endian Dolphin save rejected", !validateSaveDir(slotDir));
    mobile_ui::savefs::removeDirRecursive(slotDir);

    // Leftover backups and temp copies carry save content under a name the
    // game never opens.
    fs::create_directories(slot);
    writeFile(slot / "GameData.bin.import_rollback", valid);
    check("rollback copy rejected", !validateSaveDir(slotDir));
    mobile_ui::savefs::removeDirRecursive(slotDir);

    fs::create_directories(slot);
    writeFile(slot / "MyGameData.bin.bak", valid);
    check("mis-named save rejected", !validateSaveDir(slotDir));
    mobile_ui::savefs::removeDirRecursive(slotDir);

    fs::create_directories(slot);
    writeFile(slot / "save.bin", valid);
    check("secondary save.bin alone rejected", !validateSaveDir(slotDir));

    // The real layout: the save lives deep inside a copied title tree.
    const fs::path nested = slot / "title" / "00010000" / "524d4750" / "data";
    fs::create_directories(nested);
    writeFile(nested / "GameData.bin", valid);
    writeFile(nested / "save.bin", valid);
    check("nested valid save validates", validateSaveDir(slotDir));
    writeFile(slot / "GameData.bin.tmp", std::vector<uint8_t>(0x400, 0));
    check("junk temp file does not hide a good save", validateSaveDir(slotDir));
}

// Both durability helpers have to overwrite the copy that is already there --
// POSIX rename() does, the Windows C runtime one does not, and the host tests
// must describe the device.
void testVerifiedWritesReplaceExistingFiles() {
    Fixture fixture;
    const fs::path src = fixture.root / "src.bin";
    const fs::path dst = fixture.root / "dst.bin";
    const std::vector<uint8_t> newer = nativeSave(0x5a);
    writeFile(src, newer);
    writeFile(dst, nativeSave(0x11));
    check("verified copy replaces an existing file",
          mobile_ui::savefs::copyFileVerified(src.generic_string(), dst.generic_string()) &&
              readFile(dst) == newer);
    check("verified copy leaves no temp file", !fs::exists(dst.string() + ".tmp"));

    const fs::path atomic = fixture.root / "atomic.bin";
    writeFile(atomic, nativeSave(0x11));
    bool ok = mobile_ui::savefs::writeFileAtomic(atomic.generic_string(), newer.data(),
                                                 newer.size());
    check("atomic write replaces an existing file", ok && readFile(atomic) == newer);
    check("atomic write leaves no temp file", !fs::exists(atomic.string() + ".tmp"));
}

}  // namespace

void runSaveSyncExportChecks() {
    testValidateSaveDirRequiresALoadableSave();
    testExportWritesVerifiedCopies();
    testExportReportsWriteFailure();
    testExportRecoversInterruptedPromotion();
    testVerifiedWritesReplaceExistingFiles();
}
