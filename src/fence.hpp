#ifndef __FENCE_HPP
#define __FENCE_HPP

#include "bcn_layer.hpp"

struct fence {
	VkFence handle;
	struct device *device;
	const VkAllocationCallbacks *alloc;
};

struct fence *get_fence(VkFence);

#endif
