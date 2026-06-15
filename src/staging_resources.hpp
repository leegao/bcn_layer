#ifndef __STAGING_RESOURCES_HPP
#define __STAGING_RESOURCES_HPP

#include "buffer.hpp"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <string>

struct command_buffer;

struct TimestampQuery {
    std::string label;
    VkFormat format;
    uint64_t textureSize;
    size_t poolIndex;
    uint32_t startQueryId;
    uint32_t endQueryId;
};

struct QueryPoolBlock {
    VkQueryPool handle;
    uint32_t allocatedQueries;
};

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
    std::pair<uint32_t, uint32_t> AllocateQueryPair(
        VkCommandBuffer cmdBuf,
        const std::string& label,
        VkFormat format,
        uint64_t texture_size,
        VkQueryPool& outPool);
    
    int id = 0;
    int64_t timestamp = 0;
    
private:
    static const uint32_t kPoolBlockSize = 128;

    VkFence completed = VK_NULL_HANDLE;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkDevice device;
    
    bool freed = false;
    bool has_completed = false;
    std::vector<std::unique_ptr<struct buffer>> stagingBuffers;
    std::vector<VkImageView> stagingImageViews;
    std::vector<std::pair<VkDescriptorPool, VkDescriptorSet>> descriptorSets;
    std::vector<QueryPoolBlock> queryPools;
    std::vector<TimestampQuery> trackedQueries;
};

#endif
