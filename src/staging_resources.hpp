#ifndef __STAGING_RESOURCES_HPP
#define __STAGING_RESOURCES_HPP

#include "buffer.hpp"

#include <cstdint>
#include <vulkan/vulkan.h>
#include <vector>

struct command_buffer;

struct StagingResources {
public:
    explicit StagingResources(VkDevice device): device(device) {}
    ~StagingResources();

    std::pair<VkSemaphore, VkFence> MakeFence();
    bool IsCompleted() const { return has_completed; }
    bool IsEmpty() const { return stagingBuffers.empty() && stagingImageViews.empty(); }
    void WaitForCompletion();
    void Cleanup();
    void AddStagingBuffer(std::unique_ptr<struct buffer> buf) { stagingBuffers.push_back(std::move(buf)); }
    void AddStagingImageView(VkImageView view) { stagingImageViews.push_back(view); }
    void AddDescriptorSet(VkDescriptorPool pool, VkDescriptorSet set) { descriptorSets.push_back({ pool, set }); }
    int Size() const { return stagingBuffers.size(); }
    int MemoryUsage(VkFormat format = VK_FORMAT_UNDEFINED) const { 
        int usage = 0;
        for (const auto& buf : stagingBuffers) {
            if (format == VK_FORMAT_UNDEFINED || buf->format == format) {
                usage += buf->size;
            }
        }
        return usage;
    }
    
    int id = 0;
    int64_t timestamp = 0;
    
private:
    VkFence completed = VK_NULL_HANDLE;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkDevice device;
    
    bool freed = false;
    bool has_completed = false;
    std::vector<std::unique_ptr<struct buffer>> stagingBuffers;
    std::vector<VkImageView> stagingImageViews;
    std::vector<std::pair<VkDescriptorPool, VkDescriptorSet>> descriptorSets;
};

#endif
