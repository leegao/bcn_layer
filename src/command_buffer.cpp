#include "command_buffer.hpp"
#include "bcn_layer.hpp"
#include "image.hpp"
#include "bcn.hpp"
#include "logger.hpp"
#include "mali_gpu_profiler.hpp"
#include "pipeline_state.hpp"
#include "staging_resources.hpp"

std::unordered_map<VkCommandBuffer, std::shared_ptr<struct command_buffer>> commandBuffersMap;

struct command_buffer *
get_command_buffer(VkCommandBuffer commandbuffer)
{
	auto it = commandBuffersMap.find(commandbuffer);

	if (it == commandBuffersMap.end())
		return nullptr;

	return it->second.get();
}

VkResult DispatchOneShotAndSample(
    struct device *dev,
    std::function<void(struct command_buffer*)> record_func,
    const std::string_view shader_label)
{
    VkResult result;
    const auto& table = dev->table;

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = dev->queueFamilyIndex
    };
    VkCommandPool command_pool;
    result = table.CreateCommandPool(dev->handle, &pool_info, nullptr, &command_pool);
    if (result != VK_SUCCESS) {
        Logger::log("error", "one_shot: vkCreateCommandPool failed, result: %d", result);
        return result;
    }

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer commandBuffer;
    result = table.AllocateCommandBuffers(dev->handle, &alloc_info, &commandBuffer);
    if (result != VK_SUCCESS) {
        Logger::log("error", "one_shot: vkAllocateCommandBuffers failed, result: %d", result);
        table.DestroyCommandPool(dev->handle, command_pool, nullptr);
        return result;
    }

    // See https://vulkan.lunarg.com/doc/view/latest/linux/LoaderLayerInterface.html#creating-new-dispatchable-objects
    // To fill in the dispatch table pointer in newly created dispatchable object, the layer
    // should copy the dispatch pointer, which is always the first entry in the structure,
    // from an existing parent object of the same level (instance versus device).
    if (dev->has_more_layers) {
        *reinterpret_cast<void**>(commandBuffer) = *reinterpret_cast<void**>(dev->handle);
    }

    auto cb = std::make_shared<struct command_buffer>();
    cb->handle = commandBuffer;
    cb->device = dev;
    cb->pool = command_pool;
    cb->currentStagingResources = std::make_unique<StagingResources>(dev->handle);
    cb->reset_compute_state();

    {
        scoped_lock l(global_lock);
        commandBuffersMap[commandBuffer] = cb;
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    result = table.BeginCommandBuffer(commandBuffer, &begin_info);
    if (result != VK_SUCCESS) {
        Logger::log("error", "one_shot: vkBeginCommandBuffer failed, result: %d", result);
        goto cleanup_registry;
    }

    record_func(cb.get());

    result = table.EndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        Logger::log("error", "one_shot: vkEndCommandBuffer failed, result: %d", result);
        goto cleanup_registry;
    }

    {
        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer
        };

        VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkFence fence;
        result = table.CreateFence(dev->handle, &fence_info, nullptr, &fence);
        if (result != VK_SUCCESS) goto cleanup_registry;

        auto now = std::chrono::system_clock::now();
        cb->currentStagingResources->timestamp =
            std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();

       	if (dev->sample_gpu_counters) {
    		get_mali_gpu_profiler().Start();
    	}

        result = table.QueueSubmit(dev->queue, 1, &submit_info, fence);
        if (result != VK_SUCCESS) {
            Logger::log("error", "one_shot: vkQueueSubmit failed, result: %d", result);
        }
        table.WaitForFences(dev->handle, 1, &fence, VK_TRUE, UINT64_MAX);

       	if (dev->sample_gpu_counters) {
    	    get_mali_gpu_profiler().StopAndProcess(shader_label);
    	}

        table.DestroyFence(dev->handle, fence, nullptr);
    }

    if (cb->currentStagingResources) {
        cb->currentStagingResources->Cleanup();
    }

cleanup_registry:
    {
        scoped_lock l(global_lock);
        commandBuffersMap.erase(commandBuffer);
    }
    table.FreeCommandBuffers(dev->handle, command_pool, 1, &commandBuffer);
    table.DestroyCommandPool(dev->handle, command_pool, nullptr);

    return result;
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
		cmd->reset_compute_state();
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

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_BeginCommandBuffer(VkCommandBuffer commandBuffer,
							 const VkCommandBufferBeginInfo *pBeginInfo) {
    struct command_buffer *cb = get_command_buffer(commandBuffer);
    struct device *dev = cb->device;

    cb->reset_compute_state(); // begin/reset should clear inherited compute pipeline states

    return dev->table.BeginCommandBuffer(commandBuffer, pBeginInfo);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_ResetCommandBuffer(VkCommandBuffer commandBuffer,
    VkCommandBufferResetFlags flags) {
    struct command_buffer *cb = get_command_buffer(commandBuffer);
    struct device *dev = cb->device;

    cb->reset_compute_state(); // begin/reset should clear inherited compute pipeline states

    return dev->table.ResetCommandBuffer(commandBuffer, flags);
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_CmdBindPipeline(VkCommandBuffer commandBuffer,
    VkPipelineBindPoint pipelineBindPoint,
    VkPipeline pipeline) {
    struct command_buffer *cb = get_command_buffer(commandBuffer);
    struct device *dev = cb->device;

    if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
       	cb->computePipelineState.pipelineBound = true;
       	cb->computePipelineState.pipeline = pipeline;
    }

    dev->table.CmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_CmdBindDescriptorSets(VkCommandBuffer commandBuffer,
    VkPipelineBindPoint pipelineBindPoint,
    VkPipelineLayout layout,
    uint32_t firstSet,
    uint32_t descriptorSetCount,
    const VkDescriptorSet *pDescriptorSets,
    uint32_t dynamicOffsetCount,
    const uint32_t *pDynamicOffsets) {
    struct command_buffer *cb = get_command_buffer(commandBuffer);
    struct device *dev = cb->device;

    if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
        track_descriptor_set_binds(cb->computePipelineState, layout, firstSet,
            descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
    }

    dev->table.CmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet,
        descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_CmdBindDescriptorSets2(VkCommandBuffer commandBuffer,
    const VkBindDescriptorSetsInfo *pBindDescriptorSetsInfo) {
    struct command_buffer *cb = get_command_buffer(commandBuffer);
    struct device *dev = cb->device;

    if (pBindDescriptorSetsInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
        track_descriptor_set_binds(cb->computePipelineState,
            pBindDescriptorSetsInfo->layout,
            pBindDescriptorSetsInfo->firstSet,
            pBindDescriptorSetsInfo->descriptorSetCount,
            pBindDescriptorSetsInfo->pDescriptorSets,
            pBindDescriptorSetsInfo->dynamicOffsetCount,
            pBindDescriptorSetsInfo->pDynamicOffsets);
    }

    dev->table.CmdBindDescriptorSets2(commandBuffer, pBindDescriptorSetsInfo);
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_CmdPushConstants(VkCommandBuffer commandBuffer,
						   VkPipelineLayout layout,
						   VkShaderStageFlags stageFlags,
						   uint32_t offset,
						   uint32_t size,
						   const void *pValues) {
    struct command_buffer *cb = get_command_buffer(commandBuffer);
    struct device *dev = cb->device;

    track_push_constants(cb->computePipelineState, layout, stageFlags, offset, size, pValues);

    dev->table.CmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_CmdPushConstants2(VkCommandBuffer commandBuffer,
							const VkPushConstantsInfo *pPushConstantsInfo) {
    struct command_buffer *cb = get_command_buffer(commandBuffer);
    struct device *dev = cb->device;

    track_push_constants(cb->computePipelineState,
        pPushConstantsInfo->layout,
        pPushConstantsInfo->stageFlags,
        pPushConstantsInfo->offset,
        pPushConstantsInfo->size,
        pPushConstantsInfo->pValues);
    dev->table.CmdPushConstants2(commandBuffer, pPushConstantsInfo);
}

size_t BcnBufferSize(VkFormat format, VkBufferImageCopy& copy_region) {
   	bool formatIsBC1orBC4 = format <= VK_FORMAT_BC1_RGBA_SRGB_BLOCK
        || format == VK_FORMAT_BC4_UNORM_BLOCK
        || format == VK_FORMAT_BC4_SNORM_BLOCK;
    size_t bytesPerBlock = (formatIsBC1orBC4) ? 8 : 16;
    uint32_t rowLength = (copy_region.bufferRowLength == 0)
        ? copy_region.imageExtent.width  : copy_region.bufferRowLength;
    uint32_t imageHeight = (copy_region.bufferImageHeight == 0)
        ? copy_region.imageExtent.height : copy_region.bufferImageHeight;
    size_t blockRowLength   = (rowLength + 3) / 4;
    size_t blockImageHeight = (imageHeight + 3) / 4;
    size_t copyBlocksX = (copy_region.imageExtent.width + 3)  / 4;
    size_t copyBlocksY = (copy_region.imageExtent.height + 3) / 4;
    size_t copyBlocksZ = copy_region.imageExtent.depth;

    size_t bytesPerBlockRow   = blockRowLength * bytesPerBlock;
    size_t bytesPerBlockSlice = blockImageHeight * bytesPerBlockRow;
    return copyBlocksX * copyBlocksY * copyBlocksZ * bytesPerBlock;
}

void TranscodeAndCopyBufferToImage(
    struct device* dev,
    struct command_buffer* cb,
    struct buffer* srcBuffer,
    struct image* dstImg,
    VkImageLayout dstImageLayout,
    const VkBufferImageCopy& region,
    bool use_image_view,
    bool add_watermark)
{
    const VkLayerDispatchTable& table = dev->table;
    VkFormat format = dstImg->format;

    bool enable_transcode = dstImg->transcode_to_etc2 || dstImg->transcode_to_astc;
    int texel_size = enable_transcode ? 1 : is_bc6(format) ? 8 : 4;
    VkFormat target_format = dstImg->transcode_to_etc2 ? get_format_for_bcn_to_etc2(dev, format)
        : dstImg->transcode_to_astc ? get_format_for_bcn_to_astc(dev, format) : get_format_for_bcn(format);

    VkBufferImageCopy copy_region = region;
    int w = copy_region.imageExtent.width;
    int h = copy_region.imageExtent.height;
    int d = copy_region.imageExtent.depth;

    VkDeviceSize bcn_buffer_size = BcnBufferSize(format, copy_region);
    if (!dev->dump_buffers_path.empty() && d == 1) {
        auto bcn_buffer = create_staging_buffer(dev, bcn_buffer_size, format, w, h);
        VkBufferCopy buffer_copy_region {
            .srcOffset = copy_region.bufferOffset,
            .dstOffset = 0,
            .size = bcn_buffer_size,
        };
        table.CmdCopyBuffer(cb->handle, srcBuffer->handle, bcn_buffer->handle, 1, &buffer_copy_region);
        cb->currentStagingResources->AddStagingBuffer(std::move(bcn_buffer));
    }

    if (d == 1 || enable_transcode) {
        use_image_view = false;
    }

    if (use_image_view) {
        decompress_bcn_compute(dev, cb->handle, format, &copy_region, srcBuffer, nullptr, dstImg, dstImageLayout, add_watermark);
    } else {
        int size = w * h * d * texel_size;
        auto staging_buf = create_staging_buffer(dev, size, target_format, w, h);

        decompress_bcn_compute(dev, cb->handle, format, &copy_region, srcBuffer, staging_buf.get(), dstImg, dstImageLayout, add_watermark);

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

        table.CmdPipelineBarrier(cb->handle,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 1, &bufferBarrier, 0, nullptr);

        copy_region.bufferOffset = 0;
        copy_region.bufferRowLength = 0;
        copy_region.bufferImageHeight = 0;

        table.CmdCopyBufferToImage(cb->handle,
            staging_buf->handle, dstImg->handle, dstImageLayout, 1, &copy_region);
        cb->currentStagingResources->AddStagingBuffer(std::move(staging_buf));
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
    struct command_buffer *cb = get_command_buffer(commandBuffer);
    struct device *dev = cb->device;
    struct image *img = find_image(dstImage);
    struct buffer *buf = find_buffer(srcBuffer);
    if (!img || !buf || !img->decode_from_bcn) {
       	dev->table.CmdCopyBufferToImage(commandBuffer,
            srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
        return;
    }

    ScopedPipelineStateSnapshot snapshot(cb); // since transcoding injects a compute pipeline
    for (uint32_t i = 0; i < regionCount; i++) {
        auto scopedTimestampQuery = cb->currentStagingResources->MakeScopedTimestampQuery(
            cb, "all", img->format,
            pRegions[i].imageExtent.width * pRegions[i].imageExtent.height * pRegions[i].imageExtent.depth,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        TranscodeAndCopyBufferToImage(dev, cb, buf, img,
            dstImageLayout, pRegions[i], dev->use_image_view, dev->add_watermark);
    }
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_CmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                               const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo)
{
    struct command_buffer *cb = get_command_buffer(commandBuffer);
    struct device *dev = cb->device;
    auto dstImage = pCopyBufferToImageInfo->dstImage;
    struct image *img = find_image(dstImage);
    auto srcBuffer = pCopyBufferToImageInfo->srcBuffer;
    struct buffer *buf = find_buffer(srcBuffer);
    auto dstImageLayout = pCopyBufferToImageInfo->dstImageLayout;
    auto regionCount = pCopyBufferToImageInfo->regionCount;
    auto pRegions = pCopyBufferToImageInfo->pRegions;

    if (!img || !buf || !img->decode_from_bcn) {
        dev->table.CmdCopyBufferToImage2(commandBuffer, pCopyBufferToImageInfo);
        return;
    }

    // Emulate this using BCnLayer_CmdCopyBufferToImage since there are no valid pNexts
    ScopedPipelineStateSnapshot snapshot(cb); // since transcoding injects a compute pipeline
    for (uint32_t i = 0; i < regionCount; i++) {
        VkBufferImageCopy region {
            .bufferOffset = pRegions[i].bufferOffset,
            .bufferRowLength = pRegions[i].bufferRowLength,
            .bufferImageHeight = pRegions[i].bufferImageHeight,
            .imageSubresource = pRegions[i].imageSubresource,
            .imageOffset = pRegions[i].imageOffset,
            .imageExtent = pRegions[i].imageExtent,
        };
        auto scopedTimestampQuery = cb->currentStagingResources->MakeScopedTimestampQuery(
            cb, "all", img->format,
            region.imageExtent.width * region.imageExtent.height * region.imageExtent.depth,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        TranscodeAndCopyBufferToImage(dev, cb, buf, img, dstImageLayout,
            region, dev->use_image_view, dev->add_watermark);
    }
}

// TODO(leegao): auto-generate these
template <typename T>
std::string copy_image_info2_to_string(const int regionCount, const T* regions) {
    static_assert(std::is_same<T, VkImageCopy>::value ||
                  std::is_same<T, VkImageCopy2>::value,
                  "Unsupported type passed to copy_image_info2_to_string");
    if (!regions) {
        return "nullptr";
    }
    std::stringstream ss;
    ss << "VkCopyImageInfo {\n";
    if (regions && regionCount > 0) {
        ss << "  pRegions: [\n";
        for (uint32_t i = 0; i < regionCount; ++i) {
            const auto& region = regions[i];
            ss << "    [" << i << "] {\n";
            ss << "      srcOffset: { " << region.srcOffset.x << ", " << region.srcOffset.y << ", " << region.srcOffset.z << " }\n";
            ss << "      dstOffset: { " << region.dstOffset.x << ", " << region.dstOffset.y << ", " << region.dstOffset.z << " }\n";
            ss << "      extent: { " << region.extent.width << ", " << region.extent.height << ", " << region.extent.depth << " }\n";
            ss << "    }\n";
        }
        ss << "  ]\n";
    }
    ss << "}";
    return ss.str();
}

template <typename T>
void TranscodeAndCopyBcnImageToImage(struct device* dev,
                                     struct command_buffer* cb,
                                     struct image* srcImg,
                                     struct image* dstImg,
                                     VkImageLayout srcImageLayout,
                                     VkImageLayout dstImageLayout,
                                     const T& image_region) {
    static_assert(std::is_same<T, VkImageCopy>::value ||
                  std::is_same<T, VkImageCopy2>::value,
                  "Unsupported type passed to TranscodeAndCopyImageToImage");
    const VkLayerDispatchTable& table = dev->table;
    VkBufferImageCopy fake_pixel_region = {
        .imageExtent = {
            // map from BCn (4x4) blocks to pixels
            .width = image_region.extent.width * 4,
            .height = image_region.extent.height * 4,
            .depth = image_region.extent.depth,
        }
    };

    VkDeviceSize raw_bcn_buffer_size = BcnBufferSize(dstImg->format, fake_pixel_region);
    auto staging_block_buf = create_staging_buffer(dev, raw_bcn_buffer_size, srcImg->format,
                                                    image_region.extent.width, image_region.extent.height);
    VkBufferImageCopy copy_image_to_buffer_region = {
        .imageSubresource = image_region.srcSubresource,
        .imageOffset = image_region.srcOffset,
        .imageExtent = image_region.extent
    };

    table.CmdCopyImageToBuffer(cb->handle, srcImg->handle, srcImageLayout,
                                staging_block_buf->handle, 1, &copy_image_to_buffer_region);

    VkBufferMemoryBarrier buffer_memory_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = staging_block_buf->handle,
        .offset = 0,
        .size = VK_WHOLE_SIZE
    };

    table.CmdPipelineBarrier(cb->handle,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 1, &buffer_memory_barrier, 0, nullptr);

    VkBufferImageCopy transcode_region = {
        .imageSubresource = image_region.dstSubresource,
        .imageOffset = {
            image_region.dstOffset.x,
            image_region.dstOffset.y,
            image_region.dstOffset.z
        },
        .imageExtent = {
            image_region.extent.width * 4,
            image_region.extent.height * 4,
            image_region.extent.depth
        }
    };

    TranscodeAndCopyBufferToImage(dev, cb, staging_block_buf.get(),
                                  dstImg, dstImageLayout, transcode_region, false, dev->add_watermark);

    cb->currentStagingResources->AddStagingBuffer(std::move(staging_block_buf));
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_CmdCopyImage2(
    VkCommandBuffer commandBuffer,
    const VkCopyImageInfo2* pCopyImageInfo)
{
    struct command_buffer *cb = get_command_buffer(commandBuffer);
    struct device *dev = cb->device;
    struct image *srcImg = find_image(pCopyImageInfo->srcImage);
    struct image *dstImg = find_image(pCopyImageInfo->dstImage);

    if (!srcImg || !dstImg || !(srcImg->decode_from_bcn || dstImg->decode_from_bcn)) {
       	dev->table.CmdCopyImage2(commandBuffer, pCopyImageInfo);
       	return;
    }

    if (srcImg->format == dstImg->format) {
        // BCn -> BCn can be done via a direct vkCmdCopyImage
        dev->table.CmdCopyImage2(commandBuffer, pCopyImageInfo);
       	return;
    }

    Logger::log("info", "vkCmdCopyImage2 with incompatible formats src format: %d, dst format: %d",
        srcImg->format, dstImg->format);
    Logger::log("info", "  > pCopyImageInfo: %s",
        copy_image_info2_to_string(pCopyImageInfo->regionCount, pCopyImageInfo->pRegions).c_str());
    // Logger::log("info", "  > srcImage: %s", image_to_string(srcImg).c_str());
    // Logger::log("info", "  > dstImage: %s", image_to_string(dstImg).c_str());

    // TODO(leegao): implement the copy-from case to dump valid BCn data back somehow
    if (srcImg->decode_from_bcn) {
        Logger::log("error", "Copying from a decoded BCn texture is not currently supported");
        dev->table.CmdCopyImage2(commandBuffer, pCopyImageInfo);
        return;
    }

    ScopedPipelineStateSnapshot snapshot(cb); // since transcoding injects a compute pipeline
    for (uint32_t i = 0; i < pCopyImageInfo->regionCount; i++) {
        const auto& image_region = pCopyImageInfo->pRegions[i];
        auto scopedTimestampQuery = cb->currentStagingResources->MakeScopedTimestampQuery(
            cb, "copy_image", dstImg->format,
            image_region.extent.width * image_region.extent.height * image_region.extent.depth,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        TranscodeAndCopyBcnImageToImage(dev, cb, srcImg, dstImg, pCopyImageInfo->srcImageLayout,
            pCopyImageInfo->dstImageLayout, image_region);
    }
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_CmdCopyImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkImageCopy* pRegions)
{
    struct command_buffer *cb = get_command_buffer(commandBuffer);
    struct device *dev = cb->device;
    struct image *srcImg = find_image(srcImage);
    struct image *dstImg = find_image(dstImage);

    if (!srcImg || !dstImg || !(srcImg->decode_from_bcn || dstImg->decode_from_bcn)) {
       	dev->table.CmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
       	return;
    }

    if (srcImg->format == dstImg->format) {
        // BCn -> BCn can be done via a direct vkCmdCopyImage
        dev->table.CmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
       	return;
    }

    Logger::log("info", "vkCmdCopyImage with incompatible formats src format: %d, dst format: %d",
        srcImg->format, dstImg->format);
    Logger::log("info", "  > pCopyImageInfo: %s",
        copy_image_info2_to_string(regionCount, pRegions).c_str());
    // Logger::log("info", "  > srcImage: %s", image_to_string(srcImg).c_str());
    // Logger::log("info", "  > dstImage: %s", image_to_string(dstImg).c_str());

    // TODO(leegao): implement the copy-from case to dump valid BCn data back somehow
    if (srcImg->decode_from_bcn) {
        Logger::log("error", "Copying from a decoded BCn texture is not currently supported");
        dev->table.CmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
        return;
    }

    ScopedPipelineStateSnapshot snapshot(cb); // since transcoding injects a compute pipeline
    for (uint32_t i = 0; i < regionCount; i++) {
        const auto& image_region = pRegions[i];
        auto scopedTimestampQuery = cb->currentStagingResources->MakeScopedTimestampQuery(
            cb, "copy_image", dstImg->format,
            image_region.extent.width * image_region.extent.height * image_region.extent.depth,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        TranscodeAndCopyBcnImageToImage(dev, cb, srcImg, dstImg, srcImageLayout, dstImageLayout, image_region);
    }
}
