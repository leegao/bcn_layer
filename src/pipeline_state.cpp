#include "pipeline_state.hpp"
#include "command_buffer.hpp"

ScopedPipelineStateSnapshot::ScopedPipelineStateSnapshot(struct command_buffer *cb)
	: m_cb(cb), m_snapshot(cb->computePipelineState) {
}

ScopedPipelineStateSnapshot::~ScopedPipelineStateSnapshot() {
	struct device *dev = m_cb->device;

	if (m_snapshot.pipelineBound && m_snapshot.pipeline != VK_NULL_HANDLE) {
		dev->table.CmdBindPipeline(m_cb->handle, VK_PIPELINE_BIND_POINT_COMPUTE, m_snapshot.pipeline);
	}

	for (uint32_t setIndex = 0; setIndex < kMaxTrackedDescriptorSets; setIndex++) {
		const auto &slot = m_snapshot.sets[setIndex];
		if (!slot.valid)
			continue;

		dev->table.CmdBindDescriptorSets(m_cb->handle, VK_PIPELINE_BIND_POINT_COMPUTE, slot.layout,
			setIndex, 1, &slot.set,
			slot.dynamicOffsetCount,
			slot.dynamicOffsetCount ? slot.dynamicOffsets.data() : nullptr);
	}

	if (m_snapshot.anyPushConstantsPushed) {
		for (const auto &r : m_snapshot.pushConstantRanges) {
			if (r.offset + r.size > kMaxTrackedPushConstantBytes)
				continue;
			dev->table.CmdPushConstants(m_cb->handle, r.layout, r.stageFlags, r.offset, r.size,
				m_snapshot.pushConstantBytes.data() + r.offset);
		}
	}

	m_cb->computePipelineState = m_snapshot;
}


void track_push_constants(
    compute_bind_state &state,
    VkPipelineLayout layout,
    VkShaderStageFlags stageFlags,
    uint32_t offset,
    uint32_t size,
    const void *pValues) {
    if (!(stageFlags & VK_SHADER_STAGE_COMPUTE_BIT))
        return;

    if (offset + size <= kMaxTrackedPushConstantBytes) {
       	std::memcpy(state.pushConstantBytes.data() + offset, pValues, size);
    } else {
       	Logger::log("error", "vkCmdPushConstants: offset+size %u exceeds kMaxTrackedPushConstantBytes", offset + size);
    }
    state.anyPushConstantsPushed = true;

    bool found = false;
    for (auto &r : state.pushConstantRanges) {
       	if (r.layout == layout && r.stageFlags == stageFlags) {
            r.offset = offset;
            r.size = size;
            found = true;
            break;
       	}
    }
    if (!found)
       	state.pushConstantRanges.push_back({layout, stageFlags, offset, size});
}


void track_descriptor_set_binds(
    compute_bind_state &state,
    VkPipelineLayout layout,
    uint32_t firstSet,
    uint32_t descriptorSetCount,
    const VkDescriptorSet *pDescriptorSets,
    uint32_t dynamicOffsetCount,
    const uint32_t *pDynamicOffsets) {
    for (uint32_t i = 0; i < descriptorSetCount; i++) {
       	uint32_t setIndex = firstSet + i;
       	if (setIndex >= kMaxTrackedDescriptorSets) {
            Logger::log("error", "vkCmdBindDescriptorSets: set index %u exceeds kMaxTrackedDescriptorSet", setIndex);
            continue;
       	}

       	auto &slot = state.sets[setIndex];
       	slot.valid = true;
       	slot.set = pDescriptorSets[i];
       	slot.layout = layout;

        if (descriptorSetCount > 1 && dynamicOffsetCount > 0) {
            Logger::log("error", "vkCmdBindDescriptorSets: descriptorSetCount > 1 and dynamicOffsetCount > 0, "
            "injection save/restore will not track dynamic offsets for this call");
            continue;
        }

       	if (descriptorSetCount == 1 && dynamicOffsetCount > 0) {
            slot.dynamicOffsetCount = std::min(dynamicOffsetCount, kMaxTrackedDynamicOffsets);
            for (uint32_t k = 0; k < slot.dynamicOffsetCount; k++)
                slot.dynamicOffsets[k] = pDynamicOffsets[k];
        } else {
            slot.dynamicOffsetCount = 0;
       	}
    }
}
