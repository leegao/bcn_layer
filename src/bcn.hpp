#ifndef __BCN_HPP
#define __BCN_HPP

#include "bcn_layer.hpp"

struct push_constants {
	int width;
	int height;
	int format;
	int tile_stride;
};

bool is_supported_etc2_format(struct device *, VkFormat);
VkFormat get_format_for_etc2(VkFormat);
VkResult create_etc2_compute_pipelines(struct device *dev);
VkResult decompress_etc2_compute(struct device *dev,
                       			VkCommandBuffer commandbuffer,
                       			VkFormat format,
                       			VkBufferImageCopy *copy_region,
                       			struct buffer *srcBuffer,
                       			struct buffer *stagingBuffer);

#endif
