#include "bcn.hpp"
#include "buffer.hpp"
#include "command_buffer.hpp"
#include "image.hpp"
#include "etc2_spv.h"

VkFormat get_format_for_etc2(VkFormat format) {
    return VK_FORMAT_R8G8B8A8_UNORM; // Consider using RGB8 instead
}

bool is_supported_etc2_format(struct device *device, VkFormat format) {
   	switch(format) {
	    case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
  		case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
  		case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
  		case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
			return true;
		default:
			return false;
	}
}

static VkResult
create_new_pool(struct device *device) {
	VkResult result;
	VkLayerDispatchTable table = device->table;

	VkDescriptorPoolSize desc_sizes[] =
	{
	    {
	    	.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	    	.descriptorCount = 2
	    }
	};

	VkDescriptorPoolCreateInfo descpool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
	    .pNext = nullptr,
	    .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
	    .maxSets = 32,
	    .poolSizeCount = 1,
	    .pPoolSizes = desc_sizes
	};

	VkDescriptorPool descriptorPool;
	result = table.CreateDescriptorPool(device->handle,
		&descpool_info, NULL, &descriptorPool);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create descriptor pool, res %d", result);
		return result;
	}

	device->pools.push_back(descriptorPool);

	return VK_SUCCESS;
}

VkResult
create_etc2_compute_pipelines(struct device *dev)
{
	VkResult result;
	VkLayerDispatchTable table = dev->table;
	VkDevice device = dev->handle;

	VkShaderModule etc2ShaderModule;
	VkShaderModuleCreateInfo etc2_shader_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.codeSize = etc2_spv_len,
		.pCode = (const uint32_t *)etc2_spv,
	};

	table.CreateShaderModule(device, &etc2_shader_info, nullptr, &etc2ShaderModule);

	VkPipelineShaderStageCreateInfo shader_stage_infos[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = etc2ShaderModule,
			.pName = "main",
			.pSpecializationInfo = nullptr
		},
	};

	VkDescriptorSetLayoutBinding bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			.pImmutableSamplers = nullptr
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			.pImmutableSamplers = nullptr
		}
	};

	VkDescriptorSetLayoutCreateInfo descriptor_set_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.bindingCount = 2,
		.pBindings = bindings
	};

	result = table.CreateDescriptorSetLayout(device,
		&descriptor_set_create_info, NULL, &dev->setLayout);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create descriptor set layout, res %d", result);
		return result;
	}

	VkPushConstantRange push_constant = {
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.offset = 0,
		.size = sizeof(struct push_constants)
	};

	VkPipelineLayoutCreateInfo layout_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.setLayoutCount = 1,
		.pSetLayouts = &dev->setLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_constant
	};

	result = table.CreatePipelineLayout(device,
		&layout_create_info, NULL, &dev->layout);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create pipeline layout");
		return result;
	}

	VkComputePipelineCreateInfo pipeline_create_info[] = {
	    {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.stage = shader_stage_infos[0],
			.layout = dev->layout,
			.basePipelineHandle = VK_NULL_HANDLE,
			.basePipelineIndex = -1
		},
	};

	VkPipeline pipelines[1];

	result = table.CreateComputePipelines(device,
		VK_NULL_HANDLE, 1, pipeline_create_info, NULL, pipelines);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create compute pipeline, res %d", result);
		return result;
	}

	dev->etc2Pipeline = pipelines[0];

	table.DestroyShaderModule(device, etc2ShaderModule, nullptr);

	result = create_new_pool(dev);

	if (result != VK_SUCCESS) {
	    Logger::log("error", "Failed to create descriptor pool, res %d", result);
		return result;
	}

	return VK_SUCCESS;
}

VkResult
decompress_etc2_compute(struct device *dev,
		       		   VkCommandBuffer commandbuffer,
		       		   VkFormat format,
		       		   VkBufferImageCopy *copy_region,
		       		   struct buffer *srcBuffer,
		       		   struct buffer *stagingBuffer)
{
	VkResult result;
	VkLayerDispatchTable table;
	VkDevice device;

	table = dev->table;
	device = dev->handle;

	struct command_buffer *cb;
	{
	    scoped_lock l(global_lock);
	    cb = get_command_buffer(commandbuffer);
	    if (!cb)
	        return VK_ERROR_NOT_PERMITTED;
	}

	int width = copy_region->imageExtent.width;
	int height = copy_region->imageExtent.height;
	int offset = copy_region->bufferOffset;
	int bufferRowLength = copy_region->bufferRowLength;
	int rowLength = (bufferRowLength == 0) ? width : bufferRowLength;
	int tile_stride = (rowLength + 3) / 4;

	struct push_constants constants = {
		.width = width,
		.height = height,
		.format = format,
		.tile_stride = tile_stride,
	};

	VkDescriptorSet descriptorSet;
	VkDescriptorSetAllocateInfo desc_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = nullptr,
		.descriptorPool = dev->pools.back(),
		.descriptorSetCount = 1,
		.pSetLayouts = &dev->setLayout
	};

	result = table.AllocateDescriptorSets(device,
		&desc_alloc_info, &descriptorSet);

	// TODO: redesign this to reuse descriptors, since we're basically just using up
	// unlimited number of them right now (on many drivers, you'll also get
	// fragmented pools after 3-4 dispatches)
	if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
		create_new_pool(dev);

		desc_alloc_info.descriptorPool = dev->pools.back();

		result = table.AllocateDescriptorSets(device,
			&desc_alloc_info, &descriptorSet);
	}

	if (result != VK_SUCCESS) {
	    Logger::log("error", "Failed to allocate descriptor set: %d", result);
		return result;
	}

	VkWriteDescriptorSet desc_writes[2];

	VkDescriptorBufferInfo src_info = {
		.buffer = srcBuffer->handle,
		.offset = static_cast<VkDeviceSize>(offset),
		.range = VK_WHOLE_SIZE
	};

	VkDescriptorBufferInfo dst_info = {
		.buffer = stagingBuffer->handle,
		.offset = 0,
		.range = VK_WHOLE_SIZE
	};

	desc_writes[0] = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descriptorSet,
		.dstBinding = 1,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &src_info,
	};

	desc_writes[1] = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descriptorSet,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &dst_info,
	};

	table.UpdateDescriptorSets(device, 2, desc_writes, 0, NULL);
	table.CmdBindPipeline(commandbuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE, dev->etc2Pipeline);
	table.CmdPushConstants(commandbuffer,
		dev->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
		sizeof(constants), &constants);
	table.CmdBindDescriptorSets(commandbuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE, dev->layout, 0, 1,
		&descriptorSet, 0, nullptr);
	table.CmdDispatch(commandbuffer,
		(width + 7) / 8, (height + 7) / 8, 1);

	return VK_SUCCESS;
}
