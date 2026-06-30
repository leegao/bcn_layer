#ifndef __COMMAND_BUFFER_HPP
#define __COMMAND_BUFFER_HPP

#include "bcn_layer.hpp"
#include "buffer.hpp"
#include "fence.hpp"
#include "staging_resources.hpp"
#include "pipeline_state.hpp"

struct command_buffer {
    VkCommandBuffer handle;
    struct device *device;
    VkCommandPool pool;
    struct fence *fence;
    std::unique_ptr<StagingResources> currentStagingResources;
    compute_bind_state computePipelineState;

    void reset_compute_state() {
        computePipelineState.reset();
    }
};

struct command_buffer *get_command_buffer(VkCommandBuffer);

#endif
