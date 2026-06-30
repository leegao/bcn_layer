#ifndef PIPELINE_STATE_HPP
#define PIPELINE_STATE_HPP

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

constexpr uint32_t kMaxTrackedDescriptorSets = 32;
constexpr uint32_t kMaxTrackedPushConstantBytes = 256;
constexpr uint32_t kMaxTrackedDynamicOffsets = 8;

struct bound_descriptor_set {
    bool valid = false;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    uint32_t dynamicOffsetCount = 0;
    std::array<uint32_t, kMaxTrackedDynamicOffsets> dynamicOffsets{};
};

struct push_constant_range {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkShaderStageFlags stageFlags = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct compute_bind_state {
    bool pipelineBound = false;
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::array<bound_descriptor_set, kMaxTrackedDescriptorSets> sets{};
    std::array<uint8_t, kMaxTrackedPushConstantBytes> pushConstantBytes{};
    bool anyPushConstantsPushed = false;
    std::vector<push_constant_range> pushConstantRanges;

    void reset() {
        pipelineBound = false;
        pipeline = VK_NULL_HANDLE;
        for (auto &s : sets) s = bound_descriptor_set{};
        anyPushConstantsPushed = false;
        pushConstantRanges.clear();
    }
};

void track_push_constants(
    compute_bind_state &state,
    VkPipelineLayout layout,
    VkShaderStageFlags stageFlags,
    uint32_t offset,
    uint32_t size,
    const void *pValues);

void track_descriptor_set_binds(
    compute_bind_state &state,
    VkPipelineLayout layout,
    uint32_t firstSet,
    uint32_t descriptorSetCount,
    const VkDescriptorSet *pDescriptorSets,
    uint32_t dynamicOffsetCount,
    const uint32_t *pDynamicOffsets);

// Any injected compute pipeline must save and restore a snapshot of the pipeline
// state for the command buffer, this is done automatically by ScopedPipelineStateSnapshot
class ScopedPipelineStateSnapshot {
public:
    explicit ScopedPipelineStateSnapshot(struct command_buffer* cb);
    ~ScopedPipelineStateSnapshot();

    // No copies or moves, this is purely a scope-guard.
    ScopedPipelineStateSnapshot(const ScopedPipelineStateSnapshot&) = delete;
    ScopedPipelineStateSnapshot& operator=(const ScopedPipelineStateSnapshot&) = delete;
    ScopedPipelineStateSnapshot(ScopedPipelineStateSnapshot&& other) = delete;
    ScopedPipelineStateSnapshot& operator=(ScopedPipelineStateSnapshot&& other) = delete;

private:
    struct command_buffer* m_cb;
    compute_bind_state m_snapshot;
};

#endif // PIPELINE_STATE_HPP
