#ifndef __IMAGE_HPP
#define __IMAGE_HPP

#include "bcn_layer.hpp"
#include "bcn.hpp"

struct image {
	VkImage handle;
	VkFormat format;
	struct device *device;
	const VkAllocationCallbacks *alloc;
	bool transcode_to_etc2;
};

struct image *find_image(VkImage);

#endif
