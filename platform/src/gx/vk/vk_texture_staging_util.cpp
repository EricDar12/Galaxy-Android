#include "gx/vk/vk_texture_staging_util.h"

#include <algorithm>

namespace gpu {

TextureStagingValidation validateTextureDimensions(
    uint32_t width, uint32_t height, uint32_t levels, size_t availableBytes) {
    TextureStagingValidation res{};
    if (width == 0 || height == 0) return res;
    if (width > kMaxTextureDimension || height > kMaxTextureDimension) return res;
    if (levels > kMaxMipLevels) return res;

    const uint32_t effLevels = (levels == 0) ? 1 : levels;
    uint64_t totalBytes = 0;
    uint32_t cw = width;
    uint32_t ch = height;

    for (uint32_t l = 0; l < effLevels; ++l) {
        // Check for 32-bit/64-bit multiplication overflow
        uint64_t pixels = (uint64_t)cw * (uint64_t)ch;
        if (cw != 0 && pixels / cw != ch) return res;
        uint64_t levelBytes = pixels * 4;
        if (pixels != 0 && levelBytes / 4 != pixels) return res;

        if (totalBytes + levelBytes < totalBytes) return res; // Overflow
        totalBytes += levelBytes;

        cw = cw > 1 ? cw / 2 : 1;
        ch = ch > 1 ? ch / 2 : 1;
    }

    if (availableBytes < (size_t)totalBytes) return res;

    res.valid = true;
    res.levels = effLevels;
    res.requiredBytes = totalBytes;
    return res;
}

bool alignStagingOffset(uint64_t currentOffset, uint64_t requiredBytes,
                        uint64_t maxBatchLimit, uint64_t& outAlignedOffset) {
    if (currentOffset > UINT64_MAX - 63) return false;
    uint64_t aligned = (currentOffset + 63) & ~63ULL;
    if (aligned > UINT64_MAX - requiredBytes) return false;
    if (aligned + requiredBytes > maxBatchLimit) return false;

    outAlignedOffset = aligned;
    return true;
}

bool validateMipLevelCopyBounds(
    uint32_t level, uint32_t mipWidth, uint32_t mipHeight,
    uint64_t levelOffsetInItem, uint64_t levelBytes,
    uint64_t itemStagingOffset, uint64_t itemRequiredBytes,
    uint64_t totalBatchBytes, size_t sourceDataBytes) {
    (void)level;
    if (mipWidth == 0 || mipHeight == 0) return false;

    uint64_t pixels = (uint64_t)mipWidth * (uint64_t)mipHeight;
    if (mipWidth != 0 && pixels / mipWidth != mipHeight) return false;
    if (levelBytes != pixels * 4) return false;

    // Check bounds in source data
    if (levelOffsetInItem > UINT64_MAX - levelBytes) return false;
    if (levelOffsetInItem + levelBytes > (uint64_t)sourceDataBytes) return false;

    // Check bounds in item staging allocation
    if (levelOffsetInItem + levelBytes > itemRequiredBytes) return false;

    // Check bounds in entire batch staging buffer
    if (itemStagingOffset > UINT64_MAX - (levelOffsetInItem + levelBytes)) return false;
    uint64_t bufferOffset = itemStagingOffset + levelOffsetInItem;
    if (bufferOffset + levelBytes > totalBatchBytes) return false;

    // Vulkan copy buffer offset alignment requirement (must be multiple of 4 for RGBA8)
    if ((bufferOffset & 3) != 0) return false;

    return true;
}

} // namespace gpu
