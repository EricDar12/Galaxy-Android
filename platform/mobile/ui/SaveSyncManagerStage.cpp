// NAND staging and promotion for SaveSyncManager; see SaveSyncManagerStaging.h
// for why this lives in its own translation unit.
#include "SaveSyncManagerStaging.h"

#include "SaveStorage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
// Host-test portability; Android uses the POSIX calls directly.
#include <direct.h>
#define lstat stat
#define mkdir(path, mode) _mkdir(path)
#endif

namespace mobile_ui {
namespace stage {

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

bool removeMarioStates(const std::string& saveRoot) {
    bool cleared = true;
    for (const char* id : kTitleIds) {
        std::string path = saveRoot + "/title/00010000/" + id + "/data/mario_state.bin";
        struct stat st;
        if (lstat(path.c_str(), &st) != 0) {
            if (errno != ENOENT) cleared = false;
            continue;
        }
        if (unlink(path.c_str()) != 0) cleared = false;
    }
    return cleared;
}

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

}  // namespace stage
}  // namespace mobile_ui
