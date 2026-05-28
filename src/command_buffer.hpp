#ifndef __COMMAND_BUFFER_HPP
#define __COMMAND_BUFFER_HPP

#include "bcn_layer.hpp"
#include "buffer.hpp"
#include "fence.hpp"
#include "staging_resources.hpp"

struct command_buffer {
	VkCommandBuffer handle;
	struct device *device;
	VkCommandPool pool;
	struct fence *fence;
	std::unique_ptr<StagingResources> currentStagingResources;
};

struct command_buffer *get_command_buffer(VkCommandBuffer);

#endif
