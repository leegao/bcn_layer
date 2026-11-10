#ifndef __BUFFER_HPP
#define __BUFFER_HPP
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string_view>

struct device;

// Available does not mean the GPU has finished; reuse still needs a barrier
// unless one has already been recorded since the buffer was released.
enum class StagingReuseState {
    NotReusable,
    Available,
    Reserved,
};

struct buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkDeviceSize usedSize = 0;
    VkDeviceSize offset = 0;
    struct device *device = nullptr;
    const VkAllocationCallbacks *alloc = nullptr;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::string_view label;
    int id = 0;
    int width = 0;
    int height = 0;
    StagingReuseState stagingReuseState = StagingReuseState::NotReusable;
    uint64_t reuseBarrierCountAtRelease = 0;
    bool returnToPool = false;
    VkDeviceSize descriptorRange(VkDeviceSize descriptorOffset = 0) const
    {
        return usedSize > descriptorOffset ? usedSize - descriptorOffset : VK_WHOLE_SIZE;
    }
};

VkDeviceSize staging_buffer_aligned_size(VkFormat format, VkDeviceSize bytes);

struct buffer *find_buffer(VkBuffer);
std::unique_ptr<struct buffer> create_staging_buffer(struct device *dev, VkDeviceSize size, VkFormat format, int width, int height, std::string_view label = "");

#endif
