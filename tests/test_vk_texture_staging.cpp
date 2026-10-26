#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "gx/vk/vk_texture_staging_util.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

using namespace gpu;

static void testTextureDimensionValidation() {
    // 1. Zero dimensions
    TEST_ASSERT(!validateTextureDimensions(0, 512, 1, 1024).valid);
    TEST_ASSERT(!validateTextureDimensions(512, 0, 1, 1024).valid);

    // 2. Out of hardware limits (> 4096 or > 14 levels)
    TEST_ASSERT(!validateTextureDimensions(4097, 512, 1, 4097 * 512 * 4).valid);
    TEST_ASSERT(!validateTextureDimensions(512, 4097, 1, 512 * 4097 * 4).valid);
    TEST_ASSERT(!validateTextureDimensions(512, 512, 15, 512 * 512 * 4 * 2).valid);

    // 3. Valid dimensions & sizes
    auto v1 = validateTextureDimensions(512, 512, 1, 512 * 512 * 4);
    TEST_ASSERT(v1.valid);
    TEST_ASSERT(v1.levels == 1);
    TEST_ASSERT(v1.requiredBytes == 512 * 512 * 4);

    // 4. Mip-mapped texture (512x512, 10 levels)
    // 512x512 + 256x256 + ... + 1x1 = ~1.33x base size = 1,398,100 bytes
    auto vMip = validateTextureDimensions(512, 512, 10, 2000000);
    TEST_ASSERT(vMip.valid);
    TEST_ASSERT(vMip.levels == 10);
    TEST_ASSERT(vMip.requiredBytes > 512 * 512 * 4);

    // 5. Source buffer too small
    auto vShort = validateTextureDimensions(512, 512, 1, 512 * 512 * 4 - 1);
    TEST_ASSERT(!vShort.valid);

    printf("[PASS] Texture dimension validation contracts verified\n");
}

static void testStagingAlignmentAndOverflow() {
    uint64_t aligned = 0;

    // Normal alignment
    TEST_ASSERT(alignStagingOffset(10, 100, 1000, aligned));
    TEST_ASSERT(aligned == 64);

    // Exact multiple of 64
    TEST_ASSERT(alignStagingOffset(64, 100, 1000, aligned));
    TEST_ASSERT(aligned == 64);

    // Batch limit exceeded
    TEST_ASSERT(!alignStagingOffset(64, 1000, 500, aligned));

    // 64-bit addition overflow protection
    TEST_ASSERT(!alignStagingOffset(UINT64_MAX - 10, 100, UINT64_MAX, aligned));
    TEST_ASSERT(!alignStagingOffset(100, UINT64_MAX - 50, UINT64_MAX, aligned));

    printf("[PASS] Staging alignment and overflow protection verified\n");
}

static void testMipLevelCopyRegionBounds() {
    uint32_t w = 256, h = 256;
    uint64_t levelBytes = 256 * 256 * 4;
    size_t sourceBytes = levelBytes;
    uint64_t itemStagingOffset = 128; // 64-byte aligned
    uint64_t itemRequiredBytes = levelBytes;
    uint64_t totalBatchBytes = itemStagingOffset + itemRequiredBytes;

    // Valid single level
    TEST_ASSERT(validateMipLevelCopyBounds(0, w, h, 0, levelBytes,
                                          itemStagingOffset, itemRequiredBytes,
                                          totalBatchBytes, sourceBytes));

    // Exceeding source data bytes
    TEST_ASSERT(!validateMipLevelCopyBounds(0, w, h, 0, levelBytes,
                                           itemStagingOffset, itemRequiredBytes,
                                           totalBatchBytes, sourceBytes - 1));

    // Exceeding item staging allocation
    TEST_ASSERT(!validateMipLevelCopyBounds(0, w, h, 0, levelBytes,
                                           itemStagingOffset, itemRequiredBytes - 1,
                                           totalBatchBytes, sourceBytes));

    // Exceeding batch buffer bounds
    TEST_ASSERT(!validateMipLevelCopyBounds(0, w, h, 0, levelBytes,
                                           itemStagingOffset, itemRequiredBytes,
                                           totalBatchBytes - 1, sourceBytes));

    // Unaligned bufferOffset (not multiple of 4)
    TEST_ASSERT(!validateMipLevelCopyBounds(0, w, h, 1, levelBytes,
                                           itemStagingOffset, itemRequiredBytes + 4,
                                           totalBatchBytes + 4, sourceBytes + 4));

    // Dimension mismatch with levelBytes
    TEST_ASSERT(!validateMipLevelCopyBounds(0, w, h, 0, levelBytes - 4,
                                           itemStagingOffset, itemRequiredBytes,
                                           totalBatchBytes, sourceBytes));

    printf("[PASS] Mip level copy region bounds verified\n");
}

int main() {
    printf("=== Super Mario Galaxy Android — Texture Staging Safety Suite ===\n");
    testTextureDimensionValidation();
    testStagingAlignmentAndOverflow();
    testMipLevelCopyRegionBounds();
    printf("All texture staging safety tests passed successfully!\n");
    return 0;
}
