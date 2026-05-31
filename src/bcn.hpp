#ifndef __BCN_HPP
#define __BCN_HPP

#include "bcn_layer.hpp"
#include <cstdint>

struct push_constants {
	int format;
	int width;
	int height;
	int depth;
	int offset;	
	int bufferRowLength;
	int bufferImageHeight;
	int offsetX;
	int offsetY;
	int use_image_view;
};

struct etc2_push_constants {
    uint32_t width;
    uint32_t height;
    uint32_t flags;
};

bool is_s3tc(VkFormat);
bool is_rgtc(VkFormat);
bool is_bc6(VkFormat);
bool is_bc7(VkFormat);
bool is_supported_bcn_format(struct device *, VkFormat);
VkFormat get_format_for_bcn(VkFormat);
VkFormat get_format_for_bcn_to_etc2(struct device *, VkFormat);
VkResult create_bcn_compute_pipelines(struct device *dev);
VkResult decompress_bcn_compute(struct device *dev,
                       			VkCommandBuffer commandbuffer,
                       			VkFormat format,
                       			VkBufferImageCopy *copy_region,
                       			struct buffer *srcBuffer,
                       			struct buffer *stagingBuffer,
                       			struct image *dstImage,
                       			VkImageLayout dstImageLayout);

#endif
