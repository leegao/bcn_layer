#include "command_buffer.hpp"
#include "bcn_layer.hpp"
#include "image.hpp"
#include "bcn.hpp"
#include <vulkan/vulkan_core.h>

std::unordered_map<VkCommandBuffer, std::shared_ptr<struct command_buffer>> commandBuffersMap;

struct command_buffer *
get_command_buffer(VkCommandBuffer commandbuffer)
{
	auto it = commandBuffersMap.find(commandbuffer);

	if (it == commandBuffersMap.end())
		return nullptr;

	return it->second.get();
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_AllocateCommandBuffers(VkDevice device,
								const VkCommandBufferAllocateInfo *pAllocateInfo,
								VkCommandBuffer *pCommandBuffers)
{
	VkResult result;
	VkLayerDispatchTable table;

	struct device *dev = get_device(device);
	if (!dev)
		return VK_ERROR_INITIALIZATION_FAILED;

	table = dev->table;
	
	result = table.AllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to allocate command buffers, res %d", result);
		return result;
	}

	for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
		auto cmd = std::make_shared<struct command_buffer>();
		cmd->handle = pCommandBuffers[i];
		cmd->device = dev;
		cmd->pool = pAllocateInfo->commandPool;
		cmd->currentStagingResources = std::make_unique<StagingResources>(device);
		{
			scoped_lock l(global_lock);
			commandBuffersMap[pCommandBuffers[i]] = cmd;
		}
	}
	
	return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_FreeCommandBuffers(VkDevice device,
							VkCommandPool commandPool,
							uint32_t commandBufferCount,
							const VkCommandBuffer *pCommandBuffers)
{
	scoped_lock l(global_lock);

	struct device *dev = get_device(device);
	if (!dev)
		return;
	
	for (uint32_t i = 0; i < commandBufferCount; i++) {
		struct command_buffer *cb = get_command_buffer(pCommandBuffers[i]);
		if (!cb)
			continue;
			
	    dev->table.FreeCommandBuffers(dev->handle, commandPool, 1, &cb->handle);
		commandBuffersMap.erase(pCommandBuffers[i]);
	}
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_CmdCopyBufferToImage(VkCommandBuffer commandBuffer,
						      VkBuffer srcBuffer,
						      VkImage dstImage,
						      VkImageLayout dstImageLayout,
						      uint32_t regionCount,
						      const VkBufferImageCopy *pRegions)
{
	VkLayerDispatchTable table;
	VkResult result;

	struct command_buffer *cb = get_command_buffer(commandBuffer);
	struct device *dev = cb->device;
	struct image *img = find_image(dstImage);
	struct buffer *buf = find_buffer(srcBuffer);
	VkFormat format = img->format;
	int texel_size = (img->transcode_to_etc2 || img->transcode_to_astc) ? 1 : is_bc6(format) ? 8 : 4;
	VkFormat target_format = img->transcode_to_etc2 ? get_format_for_bcn_to_etc2(dev, format)
		: img->transcode_to_astc ? get_format_for_bcn_to_astc(dev, format) : get_format_for_bcn(format);

	table = dev->table;
	
	if (!img || !buf || !is_supported_bcn_format(dev, format)) {
		table.CmdCopyBufferToImage(commandBuffer,
			srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
		return;
	}
	
	for (uint32_t i = 0; i < regionCount; i++) {
		VkBufferImageCopy copy_region = pRegions[i];
		// UE4 games frequently use 3D textures, which we don't support in the iv shaders yet
		if (dev->use_image_view && copy_region.imageExtent.depth == 1) {
			decompress_bcn_compute(dev, commandBuffer, format, &copy_region, buf, nullptr, img, dstImageLayout);
		} else {
        	int w = copy_region.imageExtent.width;                                                         
        	int h = copy_region.imageExtent.height;
            int d = copy_region.imageExtent.depth;
        	int size = w * h * d * texel_size;
        	auto staging_buf = create_staging_buffer(dev, size, target_format, w, h);

        	decompress_bcn_compute(dev, commandBuffer, format, &copy_region, buf, staging_buf.get(), img, dstImageLayout);
        	
			VkBufferMemoryBarrier bufferBarrier = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
				.pNext = nullptr,
		    	.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		    	.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		    	.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		    	.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		    	.buffer = staging_buf->handle,
		    	.offset = 0,
		    	.size = VK_WHOLE_SIZE
			};
		
			table.CmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
				0, 0, nullptr, 1, &bufferBarrier, 0, nullptr);
	    
			copy_region.bufferOffset = 0;
			copy_region.bufferRowLength = 0;
			copy_region.bufferImageHeight = 0;

			table.CmdCopyBufferToImage(commandBuffer,
				staging_buf->handle, dstImage, dstImageLayout, 1, &copy_region);
			cb->currentStagingResources->AddStagingBuffer(std::move(staging_buf));
		}
	}
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_CmdCopyBufferToImage2(VkCommandBuffer commandBuffer, 
                               const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo)
{
	VkResult result;

	struct command_buffer *cb = get_command_buffer(commandBuffer);
	struct device *dev = cb->device;
	auto dstImage = pCopyBufferToImageInfo->dstImage;
	struct image *img = find_image(dstImage);
	auto srcBuffer = pCopyBufferToImageInfo->srcBuffer;
	struct buffer *buf = find_buffer(srcBuffer);
	VkFormat format = img->format;
	auto dstImageLayout = pCopyBufferToImageInfo->dstImageLayout;
	auto regionCount = pCopyBufferToImageInfo->regionCount;
	auto pRegions = pCopyBufferToImageInfo->pRegions;

	if (!img || !buf || !is_supported_bcn_format(dev, format)) {
		dev->table.CmdCopyBufferToImage2(commandBuffer, pCopyBufferToImageInfo);
		return;
	}

	// Emulate this using BCnLayer_CmdCopyBufferToImage since there are no valid pNexts
	std::vector<VkBufferImageCopy> regions(regionCount);
	for (uint32_t i = 0; i < regionCount; i++) {
		regions[i] = {
		    .bufferOffset = pRegions[i].bufferOffset,
		    .bufferRowLength = pRegions[i].bufferRowLength,
		    .bufferImageHeight = pRegions[i].bufferImageHeight,
		    .imageSubresource = pRegions[i].imageSubresource,
		    .imageOffset = pRegions[i].imageOffset,
		    .imageExtent = pRegions[i].imageExtent,
		};
	}

	BCnLayer_CmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, regions.data());
}
