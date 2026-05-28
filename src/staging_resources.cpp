#include "staging_resources.hpp"

#include "bcn_layer.hpp"
#include "buffer.hpp"
#include <cstdint>

std::pair<VkSemaphore, VkFence> StagingResources::MakeFence() {
    auto *dev = get_device(device);
    if (!dev || completed != VK_NULL_HANDLE)
        return {semaphore, completed};

    if (IsEmpty())
        return {semaphore, completed};

    auto [sem, fence] = dev->syncPool->Acquire();
    completed = fence;
    semaphore = sem;
    return {semaphore, completed};
}

void StagingResources::WaitForCompletion() {
    if (has_completed) return;
    auto *dev = get_device(device);
    if (!dev) return;
    dev->table.WaitForFences(device, 1, &completed, VK_TRUE, UINT64_MAX);
    has_completed = true;
}

void StagingResources::Cleanup() {
    if (freed) return;
    if (completed == VK_NULL_HANDLE) return;
    if (semaphore == VK_NULL_HANDLE) return;
    
    freed = true;

    auto *dev = get_device(device);
    if (!dev) return;

    if (completed != VK_NULL_HANDLE && semaphore != VK_NULL_HANDLE) {
        {
            scoped_lock l(global_lock);
            dev->syncPool->Release(semaphore, completed);
        }
        completed = VK_NULL_HANDLE;
        semaphore = VK_NULL_HANDLE;
    }
    
    for (auto it = stagingBuffers.begin(); it != stagingBuffers.end();) {
        auto buf = std::move(*it);
        it = stagingBuffers.erase(it);
        if (!buf) continue;

#ifdef DEBUG_BCN
        Logger::log("info", "  Peeking into buffer %p, memory %p", buf->handle, buf->memory);
        uint32_t* mappedData;
        VkResult result = dev->table.MapMemory(device, buf->memory, 0, buf->size, 0, (void **) &mappedData);
        if (result != VK_SUCCESS) {
            Logger::log("error", "    MapMemory failed: %d", result);
        }
        VkMappedMemoryRange mapped_memory_range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = buf->memory,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        dev->table.InvalidateMappedMemoryRanges(device, 1, &mapped_memory_range);
        Logger::log("info", "    StagingBuffer %p[0] = %x", buf->handle, mappedData[0]);
        Logger::log("info", "    StagingBuffer %p[1] = %x", buf->handle, mappedData[1]);
        Logger::log("info", "    StagingBuffer %p[2] = %x", buf->handle, mappedData[2]);
        Logger::log("info", "    StagingBuffer %p[3] = %x", buf->handle, mappedData[3]);
        dev->table.UnmapMemory(device, buf->memory);
#endif

        dev->table.DestroyBuffer(device, buf->handle, buf->alloc);
        dev->table.FreeMemory(device, buf->memory, buf->alloc);
    }

    for (auto it = stagingImageViews.begin(); it != stagingImageViews.end();) {
        dev->table.DestroyImageView(device, *it, dev->alloc);
        it = stagingImageViews.erase(it);
    }
}

StagingResources::~StagingResources() {
    Cleanup();
}
