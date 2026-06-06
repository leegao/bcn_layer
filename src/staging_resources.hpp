#ifndef __STAGING_RESOURCES_HPP
#define __STAGING_RESOURCES_HPP

#include "buffer.hpp"

#include <vulkan/vulkan.h>
#include <vector>

struct command_buffer;

struct StagingResources {
public:
    explicit StagingResources(VkDevice device): device(device) {}
    ~StagingResources();
    StagingResources(StagingResources&&) noexcept = default;
    StagingResources& operator=(StagingResources&&) noexcept = default;
    StagingResources(const StagingResources&) = default;
    StagingResources& operator=(const StagingResources&) = default;

    std::pair<VkSemaphore, VkFence> MakeFence();
    bool IsCompleted() const { return has_completed; }
    bool IsEmpty() const { return stagingBuffers.empty() && stagingImageViews.empty(); }
    void WaitForCompletion();
    void Cleanup();
    void AddStagingBuffer(std::unique_ptr<struct buffer> buf) { stagingBuffers.push_back(std::move(buf)); }
    void AddStagingImageView(VkImageView view) { stagingImageViews.push_back(view); }
    void AddDescriptorSet(VkDescriptorPool pool, VkDescriptorSet set) { descriptorSets.push_back({ pool, set }); }
    int Size() const { return stagingBuffers.size(); }
    
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
