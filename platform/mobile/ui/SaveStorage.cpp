#include "SaveStorage.h"

#include "DolphinSaveConverter.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#ifdef _WIN32
// Host-test portability; Android uses the POSIX calls directly.
#include <direct.h>
#include <io.h>
#include <windows.h>
#define fsync _commit
#define lstat stat
#define mkdir(path, mode) _mkdir(path)
#endif

namespace mobile_ui {
namespace savefs {

namespace {

constexpr size_t kMinSaveSize = 0x140;
constexpr size_t kMaxSaveSize = 0x10000;

bool sameContents(const std::string& a, const std::string& b) {
    FILE* fa = fopen(a.c_str(), "rb");
    FILE* fb = fopen(b.c_str(), "rb");
    bool equal = fa && fb;
    char ba[16384], bb[16384];
    while (equal) {
        size_t na = fread(ba, 1, sizeof(ba), fa);
        size_t nb = fread(bb, 1, sizeof(bb), fb);
        if (na != nb || memcmp(ba, bb, na) != 0) equal = false;
        else if (na == 0) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return equal;
}

// POSIX rename() replaces an existing target while the C runtime rename() on
// Windows fails, and overwriting the previous copy is the normal case here.
// Without this the host tests would not be describing device behaviour.
bool renameReplace(const std::string& from, const std::string& to) {
#ifdef _WIN32
    return MoveFileExA(from.c_str(), to.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(from.c_str(), to.c_str()) == 0;
#endif
}

// The game accepts only its own little-endian save layout, and only when the
// stored checksum covers the data length the header declares. Anything else --
// a torn write, a Dolphin byte-order save, a padded or renamed blob -- must
// never be promoted over live NAND data as though it were a valid slot.
bool isNativeSaveFile(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (st.st_size < static_cast<off_t>(kMinSaveSize) ||
        st.st_size > static_cast<off_t>(kMaxSaveSize)) {
        return false;
    }
    FILE* in = fopen(path.c_str(), "rb");
    if (!in) return false;
    std::vector<uint8_t> data(static_cast<size_t>(st.st_size));
    bool read = fread(data.data(), 1, data.size(), in) == data.size();
    fclose(in);
    if (!read) return false;
    return DolphinSaveConverter::isNativeSave(data.data(), data.size()) &&
           DolphinSaveConverter::hasValidChecksum(data.data(), data.size(), false);
}

}  // namespace

bool removeDirRecursive(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return errno == ENOENT;
    if (!S_ISDIR(st.st_mode)) return unlink(path.c_str()) == 0;

    DIR* dir = opendir(path.c_str());
    if (!dir) return false;
    bool ok = true;
    while (struct dirent* entry = readdir(dir)) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (!removeDirRecursive(path + "/" + entry->d_name)) ok = false;
    }
    closedir(dir);
    if (rmdir(path.c_str()) != 0) ok = false;
    return ok;
}

bool copyFileVerified(const std::string& src, const std::string& dst) {
    struct stat st;
    if (lstat(src.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) return false;

    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;

    std::string tmpDst = dst + ".tmp";
    int outFd = open(tmpDst.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0660);
    FILE* out = outFd >= 0 ? fdopen(outFd, "wb") : nullptr;
    if (!out) {
        if (outFd >= 0) close(outFd);
        fclose(in);
        return false;
    }

    char buf[16384];
    size_t n;
    size_t total = 0;
    bool writeErr = false;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            writeErr = true;
            break;
        }
        total += n;
    }
    fflush(out);
    int fd = fileno(out);
    if (fd >= 0) fsync(fd);
    fclose(out);
    fclose(in);

    if (writeErr || total != (size_t)st.st_size || !sameContents(src, tmpDst)) {
        unlink(tmpDst.c_str());
        return false;
    }

    if (!renameReplace(tmpDst, dst)) {
        unlink(tmpDst.c_str());
        return false;
    }
    return true;
}

bool copyDirVerified(const std::string& src, const std::string& dst) {
    static thread_local unsigned depth = 0;
    if (depth >= 64) return false;
    struct DepthGuard {
        unsigned& depth;
        explicit DepthGuard(unsigned& value) : depth(value) { ++depth; }
        ~DepthGuard() { --depth; }
    } guard(depth);

    struct stat srcStat;
    if (lstat(src.c_str(), &srcStat) != 0 || !S_ISDIR(srcStat.st_mode)) return false;
    if (mkdir(dst.c_str(), 0770) != 0 && errno != EEXIST) return false;
    struct stat dstStat;
    if (lstat(dst.c_str(), &dstStat) != 0 || !S_ISDIR(dstStat.st_mode)) return false;
    DIR* dir = opendir(src.c_str());
    if (!dir) return false;
    struct dirent* entry;
    bool success = true;
    while ((entry = readdir(dir)) != nullptr) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (strstr(entry->d_name, ".tmp") || strstr(entry->d_name, "_rollback") || strstr(entry->d_name, "_staging")) continue;

        std::string sPath = src + "/" + entry->d_name;
        std::string dPath = dst + "/" + entry->d_name;
        struct stat st;
        if (lstat(sPath.c_str(), &st) != 0) {
            success = false;
            break;
        }
        bool copied;
        if (S_ISDIR(st.st_mode)) {
            copied = copyDirVerified(sPath, dPath);
        } else {
            copied = S_ISREG(st.st_mode) && copyFileVerified(sPath, dPath);
        }
        if (!copied) {
            success = false;
            break;
        }
    }
    closedir(dir);
    return success;
}

bool writeFileAtomic(const std::string& path, const void* data, size_t size) {
    if (path.empty() || !data || size == 0) return false;
    std::string tmpDst = path + ".tmp";
    int fd = open(tmpDst.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0660);
    if (fd < 0) return false;

    const char* bytes = static_cast<const char*>(data);
    size_t done = 0;
    bool ok = true;
    while (done < size) {
        ssize_t n = write(fd, bytes + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        done += static_cast<size_t>(n);
    }
    if (fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;

    if (!ok || !renameReplace(tmpDst, path)) {
        unlink(tmpDst.c_str());
        return false;
    }
    // Best effort: persist the rename itself. Some Android FUSE mounts reject
    // directory fsync, and the file contents are already durable at this point.
    size_t slash = path.rfind('/');
    std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
    int dirFd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirFd >= 0) {
        fsync(dirFd);
        close(dirFd);
    }
    return true;
}

bool validateSaveDir(const std::string& dir) {
    static thread_local unsigned depth = 0;
    if (depth >= 64) return false;
    struct DepthGuard {
        unsigned& depth;
        explicit DepthGuard(unsigned& value) : depth(value) { ++depth; }
        ~DepthGuard() { --depth; }
    } guard(depth);

    struct stat st;
    if (lstat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;

    if (isNativeSaveFile(dir + "/GameData.bin")) return true;

    DIR* d = opendir(dir.c_str());
    if (!d) return false;
    struct dirent* entry;
    bool found = false;
    while ((entry = readdir(d)) != nullptr) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        std::string sub = dir + "/" + entry->d_name;
        // Only directories can still hold the save; a regular child named
        // "<something>GameData.bin<something>" is a leftover backup or a temp
        // file, and validating it would bless a tree the game cannot load.
        if (lstat(sub.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && validateSaveDir(sub)) {
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

}  // namespace savefs
}  // namespace mobile_ui
