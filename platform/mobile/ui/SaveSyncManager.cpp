#include "SaveSyncManager.h"
#include "DolphinSaveConverter.h"
#include "SaveStorage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifdef _WIN32
// Host-test portability; Android uses the POSIX calls directly.
#include <direct.h>
#define lstat stat
#define mkdir(path, mode) _mkdir(path)
#endif

namespace mobile_ui {
namespace {
constexpr size_t kMinSaveSize = 0x140;
constexpr size_t kMaxSaveSize = 0x10000;
const char* const kTitleIds[] = {"524d4750", "524d4745", "524d474a", "524d474b"};
const char* const kSaveNames[] = {"GameData.bin", "save.bin"};

std::string parentDir(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

bool makeDirs(const std::string& path) {
    if (path.empty()) return false;
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/') continue;
        std::string part = path.substr(0, i);
#ifdef _WIN32
        if (part.size() == 2 && part[1] == ':') continue;  // drive prefix, not a directory
#endif
        struct stat st;
        if (mkdir(part.c_str(), 0770) != 0 &&
            (errno != EEXIST || stat(part.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))) {
            return false;
        }
    }
    return true;
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
    for (const char* id : kTitleIds) {
        for (const char* name : kSaveNames) {
            candidates.push_back(saveRoot + "/title/00010000/" + id + "/data/" + name);
        }
    }
    for (const char* name : kSaveNames) candidates.push_back(saveRoot + "/" + name);
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
    for (const char* id : kTitleIds) {
        candidates.push_back(paths.dolphinDir + "/title/00010000/" + id + "/data/GameData.bin");
    }
    candidates.push_back(paths.mirrorDir + "/GameData.bin");
    candidates.push_back(paths.mirrorDir + "/GameData_Dolphin.bin");
    return candidates;
}

struct PromotedFile { std::string dest; std::string backup; };

bool rollback(std::vector<PromotedFile>& promoted) {
    bool restored = true;
    for (auto it = promoted.rbegin(); it != promoted.rend(); ++it) {
        if (unlink(it->dest.c_str()) != 0 && errno != ENOENT) restored = false;
        if (!it->backup.empty() && rename(it->backup.c_str(), it->dest.c_str()) != 0) {
            restored = false;
        }
    }
    promoted.clear();
    return restored;
}

// A killed promotion leaves "<file>.import_rollback" and no live file.
bool recoverInterruptedImport(const std::string& saveRoot) {
    bool recovered = true;
    for (const char* id : kTitleIds) {
        std::string destDir = std::string(saveRoot) + "/title/00010000/" + id + "/data";
        for (const char* name : kSaveNames) {
            std::string dest = destDir + "/" + name;
            std::string backup = dest + ".import_rollback";
            struct stat st;
            if (lstat(backup.c_str(), &st) != 0) {
                if (errno != ENOENT) recovered = false;
                continue;
            }
            if (lstat(dest.c_str(), &st) == 0) {
                if (unlink(backup.c_str()) != 0) recovered = false;
            } else if (rename(backup.c_str(), dest.c_str()) != 0) {
                recovered = false;
            }
        }
    }
    // Stale staging cannot shadow live NAND; writeNandSave still fails closed
    // if it cannot be cleared.
    savefs::removeDirRecursive(std::string(saveRoot) + "/import_staging");
    return recovered;
}

enum class NandWriteResult { Success, NotWritten, RolledBack, RollbackIncomplete };

// All eight copies are staged first, then renamed live. A failed promotion
// restores the previous files and reports whether that actually completed.
NandWriteResult writeNandSave(const std::string& saveRoot, const std::vector<uint8_t>& data) {
    std::string staging = saveRoot + "/import_staging";
    if (!savefs::removeDirRecursive(staging)) return NandWriteResult::NotWritten;
    bool staged = makeDirs(staging + "/title/00010000");
    for (const char* id : kTitleIds) {
        std::string rel = std::string("title/00010000/") + id + "/data";
        staged = staged && makeDirs(staging + "/" + rel);
        for (const char* name : kSaveNames) {
            staged = staged && savefs::writeFileAtomic(staging + "/" + rel + "/" + name,
                                                       data.data(), data.size());
        }
    }
    if (!staged) {
        savefs::removeDirRecursive(staging);
        return NandWriteResult::NotWritten;
    }

    std::vector<PromotedFile> promoted;
    bool ok = true;
    bool restoreFailed = false;
    for (const char* id : kTitleIds) {
        std::string destDir = saveRoot + "/title/00010000/" + id + "/data";
        if (!makeDirs(destDir)) {
            ok = false;
            break;
        }
        for (const char* name : kSaveNames) {
            std::string dest = destDir + "/" + name;
            std::string backup = dest + ".import_rollback";
            std::string stagedFile = staging + "/title/00010000/" + id + "/data/" + name;
            struct stat st;
            bool hadOld = lstat(dest.c_str(), &st) == 0;
            if (hadOld) {
                savefs::removeDirRecursive(backup);
                if (rename(dest.c_str(), backup.c_str()) != 0) {
                    ok = false;
                    break;
                }
            }
            if (rename(stagedFile.c_str(), dest.c_str()) != 0) {
                if (hadOld && rename(backup.c_str(), dest.c_str()) != 0) restoreFailed = true;
                ok = false;
                break;
            }
            promoted.push_back({dest, hadOld ? backup : std::string()});
        }
        if (!ok) break;
    }

    if (!ok) {
        bool restored = rollback(promoted) && !restoreFailed;
        savefs::removeDirRecursive(staging);
        return restored ? NandWriteResult::RolledBack : NandWriteResult::RollbackIncomplete;
    }
    for (const PromotedFile& file : promoted) {
        if (!file.backup.empty()) savefs::removeDirRecursive(file.backup);
    }
    savefs::removeDirRecursive(staging);
    return NandWriteResult::Success;
}

}  // namespace

bool SaveSyncManager::exportSave(const std::string& savePath, std::string& outToastMsg,
                                 const SaveIoPaths& paths) {
    std::string saveRoot = parentDir(savePath);
    if (saveRoot.empty()) return false;
    if (!recoverInterruptedImport(saveRoot)) {
        outToastMsg = "Interrupted save import could not be recovered; data preserved!";
        return false;
    }

    std::vector<uint8_t> data;
    if (!readFirstValid(exportCandidates(saveRoot, paths), data)) {
        outToastMsg = "No save data found yet (play & save first)!";
        return false;
    }
    if (!makeDirs(paths.downloadDir)) {
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
    if (!recoverInterruptedImport(saveRoot)) {
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

    NandWriteResult result = writeNandSave(saveRoot, data);
    if (result != NandWriteResult::Success) {
        outToastMsg = result == NandWriteResult::NotWritten
                          ? "Save import failed; NAND data was not changed!"
                          : result == NandWriteResult::RolledBack
                                ? "Save import failed; previous NAND data restored!"
                                : "Save import failed; previous NAND not fully restored, retry import!";
        return false;
    }

    bool mirrorOk = makeDirs(paths.mirrorDir) &&
                    savefs::writeFileAtomic(paths.mirrorDir + "/GameData.bin",
                                            data.data(), data.size());
    outToastMsg = wasDolphin ? "Imported & converted Dolphin save successfully!"
                             : "Imported save data successfully!";
    if (!mirrorOk) outToastMsg += " (external mirror failed)";
    return true;
}

}  // namespace mobile_ui
