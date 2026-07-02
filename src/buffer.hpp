#ifndef __BUFFER_HPP
#define __BUFFER_HPP
#include <vulkan/vulkan.h>

#include <memory>
#include <string_view>

struct device;

struct buffer {
    VkBuffer handle;
    VkDeviceMemory memory;
    VkDeviceSize size;
    VkDeviceSize offset;
    struct device *device;
    const VkAllocationCallbacks *alloc;
    VkFormat format;
    std::string_view label;
    int id;
    int width;
    int height;
};

struct buffer *find_buffer(VkBuffer);
std::unique_ptr<struct buffer> create_staging_buffer(struct device *dev, int size, VkFormat format, int width, int height, std::string_view label = "");

#endif
