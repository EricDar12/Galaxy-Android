#include "gx/vk/vk_common.h"
#include "gx/vk/vk_texture_staging_util.h"

namespace gpu {

static uint64_t sUploadAttempted = 0;
static uint64_t sUploadFailed = 0;
static uint64_t sUploadRecorded = 0;
static uint64_t sUploadBytes = 0;

struct BatchItem {
    std::shared_ptr<TexImage> img;
    VkTexture vt;
    VkDeviceSize stagingOffset;
    VkDeviceSize requiredBytes;
    uint32_t levels;
    std::vector<VkBufferImageCopy> regions;
};

void VkRenderer::uploadTextures(VkCommandBuffer cmd, const std::vector<std::shared_ptr<TexImage>>& textures) {
    if (cmd == VK_NULL_HANDLE || textures.empty()) return;

    auto flushBatch = [&](std::vector<BatchItem>& batch, VkDeviceSize totalBytes) {
        if (batch.empty()) return;
        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!createBuffer(totalBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          stagingBuf, stagingMem)) {
            LOGE("uploadTextures: batch createBuffer failed (%llu bytes)", (unsigned long long)totalBytes);
            for (auto& item : batch) {
                vkDestroyImageView(mDevice, (VkImageView)item.vt.view, nullptr);
                vkDestroyImage(mDevice, (VkImage)item.vt.image, nullptr);
                vkFreeMemory(mDevice, (VkDeviceMemory)item.vt.memory, nullptr);
                sUploadFailed++;
            }
            batch.clear();
            return;
        }

        void* mapped = nullptr;
        if (vkMapMemory(mDevice, stagingMem, 0, totalBytes, 0, &mapped) != VK_SUCCESS || !mapped) {
            LOGE("uploadTextures: batch vkMapMemory failed (%llu bytes)", (unsigned long long)totalBytes);
            vkDestroyBuffer(mDevice, stagingBuf, nullptr);
            vkFreeMemory(mDevice, stagingMem, nullptr);
            for (auto& item : batch) {
                vkDestroyImageView(mDevice, (VkImageView)item.vt.view, nullptr);
                vkDestroyImage(mDevice, (VkImage)item.vt.image, nullptr);
                vkFreeMemory(mDevice, (VkDeviceMemory)item.vt.memory, nullptr);
                sUploadFailed++;
            }
            batch.clear();
            return;
        }

        for (const auto& item : batch) {
            std::memcpy((uint8_t*)mapped + item.stagingOffset, item.img->rgba.data(), item.requiredBytes);
        }
        vkUnmapMemory(mDevice, stagingMem);

        for (const auto& item : batch) {
            VkImageMemoryBarrier barrierToDst{};
            barrierToDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrierToDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrierToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrierToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrierToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrierToDst.image = item.vt.image;
            barrierToDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrierToDst.subresourceRange.levelCount = item.vt.levels;
            barrierToDst.subresourceRange.layerCount = 1;
            barrierToDst.srcAccessMask = 0;
            barrierToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToDst);

            vkCmdCopyBufferToImage(cmd, stagingBuf, item.vt.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   (uint32_t)item.regions.size(), item.regions.data());

            VkImageMemoryBarrier barrierToShader = barrierToDst;
            barrierToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrierToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrierToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrierToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToShader);

            item.img->vkImage = (uint64_t)item.vt.image;
            item.img->vkMemory = (uint64_t)item.vt.memory;
            item.img->vkView = (uint64_t)item.vt.view;
            item.img->vkVersion = item.vt.version;
            item.img->vkGeneration = sVkDeviceGeneration;

            sUploadRecorded++;
            sUploadBytes += item.requiredBytes;
        }

        mStagingBuffersToFree.push_back({stagingBuf, stagingMem});
        batch.clear();
    };

    std::vector<BatchItem> currentBatch;
    VkDeviceSize currentBatchBytes = 0;

    for (const auto& img : textures) {
        if (!img || img->rgba.empty() || img->width == 0 || img->height == 0) continue;
        sUploadAttempted++;

        size_t availBytes = img->rgba.size() * sizeof(uint32_t);
        auto val = validateTextureDimensions(img->width, img->height, img->levels, availBytes);
        if (!val.valid) {
            LOGE("uploadTextures: invalid dimensions or size: %ux%u (%u lvls, %zu bytes)",
                 img->width, img->height, img->levels, availBytes);
            sUploadFailed++;
            continue;
        }

        bool sameDev = (img->vkGeneration == sVkDeviceGeneration);
        if (img->vkImage != 0 && img->vkVersion == img->version && sameDev) {
            continue;
        }

        if (img->vkView != 0 || img->vkImage != 0 || img->vkMemory != 0) {
            if (sameDev) {
                if (img->vkView) invalidateSamplerDescSetsForView((VkImageView)img->vkView);
                queueVkTextureDelete(img->vkImage, img->vkMemory, img->vkView);
            }
            img->vkView = img->vkImage = img->vkMemory = 0;
        }

        uint64_t alignedOffset = 0;
        if (!alignStagingOffset(currentBatchBytes, val.requiredBytes, kMaxStagingBatchBytes, alignedOffset)) {
            flushBatch(currentBatch, currentBatchBytes);
            currentBatchBytes = 0;
            if (!alignStagingOffset(currentBatchBytes, val.requiredBytes, kMaxStagingBatchBytes, alignedOffset)) {
                LOGE("uploadTextures: single texture (%llu bytes) exceeds max batch ceiling", (unsigned long long)val.requiredBytes);
                sUploadFailed++;
                continue;
            }
        }

        std::vector<VkBufferImageCopy> regions(val.levels);
        uint64_t offsetInItem = 0;
        uint32_t w = img->width, h = img->height;
        bool regionsValid = true;
        for (uint32_t l = 0; l < val.levels; l++) {
            uint64_t levelBytes = (uint64_t)w * h * 4;
            if (!validateMipLevelCopyBounds(l, w, h, offsetInItem, levelBytes,
                                           alignedOffset, val.requiredBytes,
                                           alignedOffset + val.requiredBytes, availBytes)) {
                regionsValid = false;
                break;
            }
            regions[l].bufferOffset = alignedOffset + offsetInItem;
            regions[l].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            regions[l].imageSubresource.mipLevel = l;
            regions[l].imageSubresource.layerCount = 1;
            regions[l].imageExtent = {w, h, 1};
            offsetInItem += levelBytes;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        if (!regionsValid) {
            LOGE("uploadTextures: mip region bounds validation failed for %ux%u", img->width, img->height);
            sUploadFailed++;
            continue;
        }

        VkTexture vt{};
        vt.width = img->width;
        vt.height = img->height;
        vt.levels = val.levels;
        vt.version = img->version;

        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.extent = {vt.width, vt.height, 1};
        ici.mipLevels = vt.levels;
        ici.arrayLayers = 1;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(mDevice, &ici, nullptr, &vt.image) != VK_SUCCESS) {
            LOGE("uploadTextures: vkCreateImage failed %ux%u", vt.width, vt.height);
            sUploadFailed++;
            continue;
        }

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(mDevice, vt.image, &memReqs);

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReqs.size;
        mai.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mai.memoryTypeIndex == ~0u) {
            mai.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, 0);
        }
        if (mai.memoryTypeIndex == ~0u || vkAllocateMemory(mDevice, &mai, nullptr, &vt.memory) != VK_SUCCESS) {
            LOGE("uploadTextures: vkAllocateMemory failed (memType=%u, size=%llu)", mai.memoryTypeIndex, (unsigned long long)memReqs.size);
            vkDestroyImage(mDevice, vt.image, nullptr);
            sUploadFailed++;
            continue;
        }
        if (vkBindImageMemory(mDevice, vt.image, vt.memory, 0) != VK_SUCCESS) {
            LOGE("uploadTextures: vkBindImageMemory failed");
            vkDestroyImage(mDevice, vt.image, nullptr);
            vkFreeMemory(mDevice, vt.memory, nullptr);
            sUploadFailed++;
            continue;
        }

        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = vt.image;
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = vt.levels;
        ivci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(mDevice, &ivci, nullptr, &vt.view) != VK_SUCCESS) {
            LOGE("uploadTextures: vkCreateImageView failed");
            vkDestroyImage(mDevice, vt.image, nullptr);
            vkFreeMemory(mDevice, vt.memory, nullptr);
            sUploadFailed++;
            continue;
        }

        currentBatchBytes = alignedOffset + val.requiredBytes;
        currentBatch.push_back({img, vt, alignedOffset, val.requiredBytes, val.levels, std::move(regions)});
    }

    if (!currentBatch.empty()) {
        flushBatch(currentBatch, currentBatchBytes);
    }

    if (sUploadRecorded % 200 == 0 && sUploadRecorded > 0) {
        LOGI("VkRenderer textures: %llu attempted, %llu recorded, %llu failed, %llu KB staged",
             (unsigned long long)sUploadAttempted, (unsigned long long)sUploadRecorded,
             (unsigned long long)sUploadFailed, (unsigned long long)(sUploadBytes / 1024));
    }
}

} // namespace gpu
