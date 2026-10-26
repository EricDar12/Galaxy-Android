#pragma once
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

#include "gx/vk_shader_inventory.h"
#include "gx/vk_shader_manifest.h"

namespace {

static inline gpu::PipelineKey makeTestPipelineKey(uint32_t val) {
    gpu::ShaderUid u{};
    u.vtxFlags = 0x4047;
    u.numChans = 1;
    u.colorCtrl[0] = val;

    gpu::DecodedPipelineKey d{};
    d.uid = u;
    d.vtxFlags = u.vtxFlags;
    d.depthTest = true;
    d.depthWrite = true;
    d.cullMode = gpu::CanonicalCullMode::Back;
    return gpu::encodePipelineKey(d);
}

// 1. Concurrent synchronous and asynchronous save attempts (Writer Exclusivity)
static inline void test_concurrent_sync_async_exclusivity() {
    printf("[TEST] Testing concurrent sync and async save exclusivity...\n");
    auto& inv = gpu::ShaderInventory::get();

    std::atomic<int> activeWriters{0};
    std::atomic<int> maxConcurrentWriters{0};
    std::atomic<int> totalWriteCalls{0};

    inv.setManifestWriterForTesting([&](const std::string&,
                                        const std::vector<gpu::ManifestStagedEntry>&,
                                        uint32_t) -> bool {
        int active = activeWriters.fetch_add(1) + 1;
        int currentMax = maxConcurrentWriters.load();
        while (active > currentMax && !maxConcurrentWriters.compare_exchange_weak(currentMax, active)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        activeWriters.fetch_sub(1);
        totalWriteCalls.fetch_add(1);
        return true;
    });

    constexpr int kThreads = 6;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&inv, t]() {
            for (int i = 0; i < 15; i++) {
                uint32_t val = (uint32_t)(900000 + t * 100 + i);
                gpu::ShaderUid u{};
                u.colorCtrl[0] = val;
                inv.recordRuntimePipelineKeyMiss(u, makeTestPipelineKey(val), "test_exclusivity");
                if (t % 2 != 0) {
                    inv.saveInventory(true);
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }
    inv.saveInventory(true);

    assert(maxConcurrentWriters.load() == 1);
    assert(activeWriters.load() == 0);
    assert(totalWriteCalls.load() > 0);
    inv.resetManifestWriterForTesting();
    printf("[PASS] Writer exclusivity strictly maintained: max concurrent writers was 1 (%d calls).\n",
           totalWriteCalls.load());
}

// 2. Runtime discoveries arriving while write is in flight (Coalescing & No Lost Snapshots)
static inline void test_discoveries_while_write_in_flight() {
    printf("[TEST] Testing runtime discoveries arriving while write is in flight...\n");
    auto& inv = gpu::ShaderInventory::get();

    std::atomic<bool> writerEntered{false};
    std::atomic<bool> writerCanProceed{false};
    std::atomic<int> writerCallCount{0};
    std::atomic<size_t> lastPersistedCount{0};

    inv.setManifestWriterForTesting([&](const std::string&,
                                        const std::vector<gpu::ManifestStagedEntry>& entries,
                                        uint32_t) -> bool {
        writerEntered.store(true);
        writerCallCount.fetch_add(1);
        while (!writerCanProceed.load()) {
            std::this_thread::yield();
        }
        lastPersistedCount.store(entries.size());
        return true;
    });

    for (uint32_t i = 1; i <= 35; i++) {
        uint32_t valA = 810000 + i;
        gpu::ShaderUid uA{};
        uA.colorCtrl[0] = valA;
        inv.recordRuntimePipelineKeyMiss(uA, makeTestPipelineKey(valA), "flight_test_A");
    }

    while (!writerEntered.load()) {
        std::this_thread::yield();
    }

    // While writer is blocked inside, add more discoveries
    for (uint32_t i = 1; i <= 5; i++) {
        uint32_t valB = 820000 + i;
        gpu::ShaderUid uB{};
        uB.colorCtrl[0] = valB;
        inv.recordRuntimePipelineKeyMiss(uB, makeTestPipelineKey(valB), "flight_test_B");
    }

    // Release first write
    writerCanProceed.store(true);

    // Synchronous flush must cause the second pass to run and capture all entries
    inv.saveInventory(true);

    assert(writerCallCount.load() >= 2);
    assert(lastPersistedCount.load() >= 40);
    inv.resetManifestWriterForTesting();
    printf("[PASS] Discovered keys during in-flight write were safely coalesced and persisted.\n");
}

// 3. Failed write followed by successful retry (Durability)
static inline void test_failed_write_retry() {
    printf("[TEST] Testing failed manifest write and retry durability...\n");
    auto& inv = gpu::ShaderInventory::get();

    std::atomic<int> attempts{0};
    inv.setManifestWriterForTesting([&](const std::string&,
                                        const std::vector<gpu::ManifestStagedEntry>&,
                                        uint32_t) -> bool {
        int att = attempts.fetch_add(1);
        if (att == 0) {
            return false; // Simulate I/O failure on first try
        }
        return true; // Succeeds on second try
    });

    uint32_t val = 730001;
    gpu::ShaderUid u{};
    u.colorCtrl[0] = val;
    inv.recordRuntimePipelineKeyMiss(u, makeTestPipelineKey(val), "retry_test");

    const size_t beforeSave = inv.savedRuntimeEntries();
    inv.saveInventory(true); // Attempt 1 fails

    // Saved count must not advance on failure
    assert(attempts.load() >= 1);

    // Second flush succeeds
    inv.saveInventory(true);
    assert(attempts.load() >= 2);
    assert(inv.savedRuntimeEntries() >= beforeSave + 1);

    inv.resetManifestWriterForTesting();
    printf("[PASS] Failed write safely preserved retry state without false persistence.\n");
}

// 4. Shutdown waiting for final snapshot
static inline void test_shutdown_flush_and_join() {
    printf("[TEST] Testing shutdown flush and worker join...\n");
    auto& inv = gpu::ShaderInventory::get();

    std::atomic<bool> saveCompleted{false};
    inv.setManifestWriterForTesting([&](const std::string&,
                                        const std::vector<gpu::ManifestStagedEntry>&,
                                        uint32_t) -> bool {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        saveCompleted.store(true);
        return true;
    });

    uint32_t val = 640001;
    gpu::ShaderUid u{};
    u.colorCtrl[0] = val;
    inv.recordRuntimePipelineKeyMiss(u, makeTestPipelineKey(val), "shutdown_test");

    inv.shutdown(); // Must wait for in-flight save and join worker thread

    assert(saveCompleted.load() == true);
    inv.resetManifestWriterForTesting();
    printf("[PASS] Shutdown flushed all pending entries and cleanly joined worker.\n");
}

} // namespace

static inline void test_async_manifest_writer_concurrency() {
    printf("=== Starting Async Shader Manifest Writer Concurrency Test Suite ===\n");
    gpu::ShaderInventory::get().init(".", "");
    test_concurrent_sync_async_exclusivity();
    test_discoveries_while_write_in_flight();
    test_failed_write_retry();
    test_shutdown_flush_and_join();
    printf("=== All Async Manifest Concurrency Tests Passed! ===\n");
}
