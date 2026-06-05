#ifndef __BCN_HPP
#define __BCN_HPP

#include "bcn_layer.hpp"

struct push_constants {
	int width;
	int height;
	int format;
	int tile_stride;
};

struct astc_push_constants {
    uint32_t error_color[4]; // 16 bytes, maps to uvec4
    int32_t width;           // 4 bytes  \ maps to ivec2 resolution
    int32_t height;          // 4 bytes  /
};

bool is_supported_etc2_format(struct device *, VkFormat);
bool is_supported_astc_format(struct device *, VkFormat);

VkFormat get_format_for_etc2(VkFormat);
VkFormat get_format_for_astc(VkFormat);

VkResult create_etc2_compute_pipelines(struct device *dev);
VkResult create_astc_compute_pipelines(struct device *dev);
VkResult decompress_etc2_compute(struct device *dev,
                       			VkCommandBuffer commandbuffer,
                       			VkFormat format,
                       			VkBufferImageCopy *copy_region,
                       			struct buffer *srcBuffer,
                       			struct buffer *stagingBuffer);
VkResult decompress_astc_compute(struct device *dev,
                       			VkCommandBuffer commandbuffer,
                       			VkFormat format,
                       			VkBufferImageCopy *copy_region,
                       			struct buffer *srcBuffer,
                       			struct buffer *stagingBuffer);

#endif
