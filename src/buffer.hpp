#ifndef __BUFFER_HPP
#define __BUFFER_HPP
#include <vulkan/vulkan.h>

#include <memory>

struct device;

struct buffer {
    VkBuffer handle;
    VkDeviceMemory memory;
    VkDeviceSize size;
    VkDeviceSize offset;
    struct device *device;
    const VkAllocationCallbacks *alloc;
};

struct buffer *find_buffer(VkBuffer);
std::unique_ptr<struct buffer> create_staging_buffer(struct device *dev, int size);

#endif
