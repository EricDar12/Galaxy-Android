#pragma once

#include <cstddef>
#include <cstdint>

namespace gpu {

constexpr uint64_t kMaxStagingBatchBytes = 128ULL * 1024 * 1024; // 128 MiB
constexpr uint32_t kMaxTextureDimension = 4096;
constexpr uint32_t kMaxMipLevels = 14;

struct MipLevelBounds {
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t byteSize = 0;
    uint64_t offsetInItem = 0;
};

struct TextureStagingValidation {
    bool valid = false;
    uint32_t levels = 1;
    uint64_t requiredBytes = 0;
};

// Validates texture dimensions, levels, arithmetic overflow, and available source memory.
TextureStagingValidation validateTextureDimensions(
    uint32_t width, uint32_t height, uint32_t levels, size_t availableBytes);

// Safely computes 64-byte aligned offset, checking for arithmetic overflow and batch ceiling.
bool alignStagingOffset(uint64_t currentOffset, uint64_t requiredBytes,
                        uint64_t maxBatchLimit, uint64_t& outAlignedOffset);

// Validates bounds for each mip-level copy against source data, item staging, and batch staging.
bool validateMipLevelCopyBounds(
    uint32_t level, uint32_t mipWidth, uint32_t mipHeight,
    uint64_t levelOffsetInItem, uint64_t levelBytes,
    uint64_t itemStagingOffset, uint64_t itemRequiredBytes,
    uint64_t totalBatchBytes, size_t sourceDataBytes);

} // namespace gpu
