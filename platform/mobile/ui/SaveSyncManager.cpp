#include "SaveSyncManager.h"
#include "DolphinSaveConverter.h"
#include "SaveStorage.h"
#include "SaveSyncManagerStaging.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace mobile_ui {
namespace {
constexpr size_t kMinSaveSize = 0x140;
constexpr size_t kMaxSaveSize = 0x10000;

std::string parentDir(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

bool readSaveFile(const std::string& path, std::vector<uint8_t>& out) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (st.st_size < static_cast<off_t>(kMinSaveSize) ||
        st.st_size > static_cast<off_t>(kMaxSaveSize)) {
        return false;
    }
    FILE* in = fopen(path.c_str(), "rb");
    if (!in) return false;
    out.resize(static_cast<size_t>(st.st_size));
    bool ok = fread(out.data(), 1, out.size(), in) == out.size() && ferror(in) == 0;
    fclose(in);
    if (!ok) out.clear();
    return ok;
}

bool readFirstValid(const std::vector<std::string>& candidates, std::vector<uint8_t>& out) {
    for (const std::string& path : candidates) {
        if (readSaveFile(path, out)) return true;
    }
    return false;
}

std::vector<std::string> exportCandidates(const std::string& saveRoot, const SaveIoPaths& paths) {
    std::vector<std::string> candidates;
    for (const char* id : stage::kTitleIds) {
        for (const char* name : stage::kSaveNames) {
            candidates.push_back(saveRoot + "/title/00010000/" + id + "/data/" + name);
        }
    }
    for (const char* name : stage::kSaveNames) candidates.push_back(saveRoot + "/" + name);
    candidates.push_back(paths.mirrorDir + "/GameData.bin");
    candidates.push_back(paths.mirrorDir + "/GameData_Dolphin.bin");
    return candidates;
}

std::vector<std::string> importCandidates(const SaveIoPaths& paths) {
    std::vector<std::string> candidates;
    for (const char* name : {"GalaxyAndroid_save.bin", "GameData.bin",
                             "GameData_Dolphin.bin", "save.bin"}) {
        candidates.push_back(paths.downloadDir + "/" + name);
    }
    for (const char* id : stage::kTitleIds) {
        candidates.push_back(paths.dolphinDir + "/title/00010000/" + id + "/data/GameData.bin");
    }
    candidates.push_back(paths.mirrorDir + "/GameData.bin");
    candidates.push_back(paths.mirrorDir + "/GameData_Dolphin.bin");
    return candidates;
}

}  // namespace

bool SaveSyncManager::exportSave(const std::string& savePath, std::string& outToastMsg,
                                 const SaveIoPaths& paths) {
    std::string saveRoot = parentDir(savePath);
    if (saveRoot.empty()) return false;
    if (!stage::recoverInterruptedImport(saveRoot)) {
        outToastMsg = "Interrupted save import could not be recovered; data preserved!";
        return false;
    }

    std::vector<uint8_t> data;
    if (!readFirstValid(exportCandidates(saveRoot, paths), data)) {
        outToastMsg = "No save data found yet (play & save first)!";
        return false;
    }
    if (!stage::makeDirs(paths.downloadDir)) {
        outToastMsg = "Could not create Download folder!";
        return false;
    }

    bool raw1 = savefs::writeFileAtomic(paths.downloadDir + "/GalaxyAndroid_save.bin",
                                        data.data(), data.size());
    bool raw2 = savefs::writeFileAtomic(paths.downloadDir + "/GameData.bin",
                                        data.data(), data.size());
    if (!raw1 || !raw2) {
        outToastMsg = "Export failed: could not write the save file!";
        return false;
    }

    std::vector<uint8_t> dolphinData;
    bool dolphinOk = DolphinSaveConverter::convertNativeToDolphin(data.data(), data.size(), dolphinData) &&
                     savefs::writeFileAtomic(paths.downloadDir + "/GameData_Dolphin.bin",
                                             dolphinData.data(), dolphinData.size());
    outToastMsg = dolphinOk ? "Exported to Download/ (includes Dolphin save)!"
                            : "Exported save to Download/ (Dolphin copy failed)!";
    return true;
}

bool SaveSyncManager::importSave(const std::string& savePath, std::string& outToastMsg,
                                 const SaveIoPaths& paths) {
    std::string saveRoot = parentDir(savePath);
    if (saveRoot.empty()) {
        outToastMsg = "Save path not set!";
        return false;
    }
    if (!stage::recoverInterruptedImport(saveRoot)) {
        outToastMsg = "Interrupted save import could not be recovered; data preserved!";
        return false;
    }

    std::vector<uint8_t> data;
    bool foundAny = false;
    for (const std::string& path : importCandidates(paths)) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0) foundAny = true;
        if (readSaveFile(path, data)) break;
    }
    if (data.empty()) {
        outToastMsg = foundAny ? "Save file is too small, too large or unreadable!"
                               : "Put GameData.bin in Download or Dolphin folder!";
        return false;
    }

    bool wasDolphin = false;
    if (!DolphinSaveConverter::prepareForImport(data, wasDolphin)) {
        outToastMsg = wasDolphin ? "Failed to convert Dolphin save (corrupt or invalid)!"
                                 : "Save format unrecognized or corrupted!";
        return false;
    }

    stage::NandWriteResult result = stage::writeNandSave(saveRoot, data);
    if (result != stage::NandWriteResult::Success) {
        outToastMsg = result == stage::NandWriteResult::NotWritten
                          ? "Save import failed; NAND data was not changed!"
                          : result == stage::NandWriteResult::RolledBack
                                ? "Save import failed; previous NAND data restored!"
                                : "Save import failed; previous NAND not fully restored, retry import!";
        return false;
    }

    bool mirrorOk = stage::makeDirs(paths.mirrorDir) &&
                    savefs::writeFileAtomic(paths.mirrorDir + "/GameData.bin",
                                            data.data(), data.size());
    bool statesCleared = stage::removeMarioStates(saveRoot);
    outToastMsg = wasDolphin ? "Imported & converted Dolphin save successfully!"
                             : "Imported save data successfully!";
    if (!mirrorOk) outToastMsg += " (external mirror failed)";
    if (!statesCleared) outToastMsg += " (saved position not cleared)";
    return true;
}

}  // namespace mobile_ui
