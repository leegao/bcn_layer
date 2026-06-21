#ifndef __IMAGE_HPP
#define __IMAGE_HPP

#include "bcn_layer.hpp"
#include "bcn.hpp"

#include <vulkan/vulkan.h>
#include <string>

struct image {
	VkImage handle;
	VkFormat format;
	struct device *device;
	const VkAllocationCallbacks *alloc;
	bool decode_from_bcn;
	bool transcode_to_etc2;
	bool transcode_to_astc;
	VkImageCreateInfo create_info;
	std::vector<VkStructureType> pnexts;
};

struct image *find_image(VkImage);
std::string image_to_string(const image* img);

#endif
