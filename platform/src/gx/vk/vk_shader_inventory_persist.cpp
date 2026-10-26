// Runtime-discovered pipeline keys, cached between launches.
//
// The cooked manifest can only describe what the asset data knows. Keys the
// renderer actually built are ground truth, so they are appended to the
// inventory cache and prewarmed on the next launch. The file uses the manifest
// on-disk format, which already carries a schema/build version and per-record
// CRCs, so a stale cache is rejected instead of misread.

#include "gx/vk_shader_inventory.h"
#include "gx/vk_shader_manifest.h"
#include "port/port.h"

#include <vector>
#include <thread>
#include <chrono>

namespace gpu {

namespace {
constexpr size_t kMaxCachedKeys = 4096;
constexpr size_t kSaveEveryNthNewKey = 32;
}  // namespace

ShaderInventory::~ShaderInventory() {
    shutdown();
}

void ShaderInventory::ensureWorkerStartedLocked() {
    if (mWorkerRunning) return;
    mStopWorker = false;
    mWorkerThread = std::thread(&ShaderInventory::workerLoop, this);
    mWorkerRunning = mWorkerThread.joinable();
}

void ShaderInventory::workerLoop() {
    while (true) {
        std::vector<ManifestStagedEntry> staged;
        std::string path;
        uint64_t snapshottedCount = 0;
        ManifestWriterFn writer;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCv.wait(lock, [this]() {
                return mStopWorker || mSaveRequested;
            });
            if (mStopWorker && !mSaveRequested) {
                break;
            }
            mSaveRequested = false;
            mWorkerActive = true;
            snapshottedCount = mStats.runtimeDiscoveredEntries;
            path = mInventoryFilePath;
            writer = mWriterFn ? mWriterFn : &writeAssetManifest;

            if (!path.empty() && !mEntries.empty()) {
                staged.reserve(mEntries.size());
                for (const auto& e : mEntries) {
                    if (!e.isRuntimeDiscovered) continue;
                    if (staged.size() >= kMaxCachedKeys) break;
                    ManifestStagedEntry s{};
                    s.uid = e.uid;
                    s.key = e.key;
                    s.source = e.sourceAsset;
                    staged.push_back(std::move(s));
                }
            }
        }

        bool success = false;
        if (!staged.empty() && (!path.empty() || mWriterFn)) {
            success = writer(path, staged, kBuildVersion);
        } else if (path.empty() || staged.empty()) {
            success = true;
        }

        {
            std::lock_guard<std::mutex> lock(mMutex);
            mWorkerActive = false;
            mSavePassCount++;
            if (success) {
                mSavedRuntimeEntries = snapshottedCount;
                if (!staged.empty()) {
                    port_log("vk_shader_inventory: cached %zu runtime keys for next launch", staged.size());
                }
            } else {
                LOGW("vk_shader_inventory: could not cache %zu runtime keys to %s (will retry)",
                     staged.size(), path.c_str());
                mSaveRequested = true;
            }
            mDoneCv.notify_all();
        }

        if (mStopWorker && !mSaveRequested) {
            break;
        }
    }
}

void ShaderInventory::saveInventory(bool synchronous) {
    std::unique_lock<std::mutex> lock(mMutex);
    if (mInventoryFilePath.empty() && !mWriterFn) return;
    if (mEntries.empty()) return;

    if (!synchronous) {
        if (mStats.runtimeDiscoveredEntries < mSavedRuntimeEntries + kSaveEveryNthNewKey) {
            return;
        }
        mSaveRequested = true;
        ensureWorkerStartedLocked();
        mCv.notify_one();
        return;
    }

    if (mStats.runtimeDiscoveredEntries == mSavedRuntimeEntries && !mSaveRequested && !mWorkerActive) {
        return;
    }

    ensureWorkerStartedLocked();
    if (!mWorkerRunning) {
        std::vector<ManifestStagedEntry> staged;
        staged.reserve(mEntries.size());
        for (const auto& e : mEntries) {
            if (!e.isRuntimeDiscovered) continue;
            if (staged.size() >= kMaxCachedKeys) break;
            ManifestStagedEntry s{};
            s.uid = e.uid;
            s.key = e.key;
            s.source = e.sourceAsset;
            staged.push_back(std::move(s));
        }
        const std::string path = mInventoryFilePath;
        const uint64_t targetSeq = mStats.runtimeDiscoveredEntries;
        auto writer = mWriterFn ? mWriterFn : &writeAssetManifest;
        lock.unlock();

        if (!path.empty() && !staged.empty()) {
            if (writer(path, staged, kBuildVersion)) {
                std::lock_guard<std::mutex> reLock(mMutex);
                mSavedRuntimeEntries = targetSeq;
                port_log("vk_shader_inventory: cached %zu runtime keys for next launch", staged.size());
            }
        }
        return;
    }

    mSaveRequested = true;
    const uint64_t startPass = mSavePassCount;
    const uint64_t targetSeq = mStats.runtimeDiscoveredEntries;
    mCv.notify_one();

    mDoneCv.wait(lock, [this, startPass, targetSeq]() {
        return (mSavedRuntimeEntries >= targetSeq) ||
               (mSavePassCount > startPass && !mSaveRequested) ||
               !mWorkerRunning;
    });
}

void ShaderInventory::shutdown() {
    saveInventory(true);

    std::thread toJoin;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mWorkerRunning) return;
        mStopWorker = true;
        mCv.notify_all();
        toJoin = std::move(mWorkerThread);
        mWorkerRunning = false;
    }
    if (toJoin.joinable()) {
        toJoin.join();
    }
}

void ShaderInventory::setManifestWriterForTesting(ManifestWriterFn fn) {
    std::lock_guard<std::mutex> lock(mMutex);
    mWriterFn = std::move(fn);
}

void ShaderInventory::resetManifestWriterForTesting() {
    std::lock_guard<std::mutex> lock(mMutex);
    mWriterFn = nullptr;
}

size_t ShaderInventory::savedRuntimeEntries() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mSavedRuntimeEntries;
}

void ShaderInventory::loadInventory() {
    std::vector<ManifestStagedEntry> staged;
    uint32_t build = 0;
    if (mInventoryFilePath.empty()) return;
    if (!parseAssetManifest(mInventoryFilePath, staged, build) || staged.empty()) return;

    std::lock_guard<std::mutex> lock(mMutex);
    size_t published = 0;
    for (const auto& item : staged) {
        if (mKeyIndex.find(item.key) != mKeyIndex.end()) continue;
        addEntryLocked(item.uid, item.key, item.source, true);
        published++;
    }
    mSavedRuntimeEntries = mStats.runtimeDiscoveredEntries;
    port_log("vk_shader_inventory: restored %zu runtime keys from %s", published, mInventoryFilePath.c_str());
}

}  // namespace gpu
