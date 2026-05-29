#include "bcn.hpp"
#include "buffer.hpp"
#include "command_buffer.hpp"
#include "image.hpp"
#include "s3tc_spv.h"
#include "s3tc_iv_spv.h"
#include "bc6_spv.h"
#include "bc6_iv_spv.h"
#include "bc7_spv.h"
#include "bc7_iv_spv.h"
#include "rgtc_spv.h"
#include "rgtc_iv_spv.h"
#include "etc2_encode_spv.h"
#include <memory>
#include <vulkan/vulkan_core.h>

bool is_s3tc(VkFormat format) {
	switch (format) {
		case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
		case VK_FORMAT_BC2_UNORM_BLOCK:
		case VK_FORMAT_BC2_SRGB_BLOCK:
		case VK_FORMAT_BC3_UNORM_BLOCK:
		case VK_FORMAT_BC3_SRGB_BLOCK:
			return true;
		default:
			return false;
	}
}

bool is_rgtc(VkFormat format) {
	switch (format) {
		case VK_FORMAT_BC4_UNORM_BLOCK:
		case VK_FORMAT_BC4_SNORM_BLOCK:
		case VK_FORMAT_BC5_UNORM_BLOCK:
		case VK_FORMAT_BC5_SNORM_BLOCK:
			return true;
		default:
		    return false;
	}
}

bool is_bc6(VkFormat format) {
	switch(format) {
		case VK_FORMAT_BC6H_UFLOAT_BLOCK:
		case VK_FORMAT_BC6H_SFLOAT_BLOCK:
			return true;
		default:
			return false;
	}
}

bool is_bc7(VkFormat format) {
	switch (format) {
		case VK_FORMAT_BC7_SRGB_BLOCK:
		case VK_FORMAT_BC7_UNORM_BLOCK:
			return true;
		default:
			return false;
	}
}

VkFormat get_format_for_bcn(VkFormat format) {
	switch (format) {
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
		case VK_FORMAT_BC2_SRGB_BLOCK:
		case VK_FORMAT_BC3_SRGB_BLOCK:
		case VK_FORMAT_BC7_SRGB_BLOCK:
		case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
		case VK_FORMAT_BC2_UNORM_BLOCK:
		case VK_FORMAT_BC3_UNORM_BLOCK:
		case VK_FORMAT_BC4_UNORM_BLOCK:
		case VK_FORMAT_BC5_UNORM_BLOCK:
		case VK_FORMAT_BC7_UNORM_BLOCK:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case VK_FORMAT_BC4_SNORM_BLOCK:
		case VK_FORMAT_BC5_SNORM_BLOCK:
			return VK_FORMAT_R8G8B8A8_SNORM;
		default:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
	}
}

VkFormat get_format_for_bcn_to_etc2(struct device *device, VkFormat format) {
	return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK; // TODO: opportunistically use RGB8
}

bool is_supported_bcn_format(struct device *device, VkFormat format) {
    VkPhysicalDeviceProperties2 props2 = device->props2;
    VkPhysicalDeviceDriverProperties driverProps = device->driverProps;

    if (device->compute_bcn_auto && ((driverProps.driverID == VK_DRIVER_ID_QUALCOMM_PROPRIETARY && props2.properties.driverVersion > VK_MAKE_VERSION(512, 502, 0)) ||
                                               driverProps.driverID == VK_DRIVER_ID_MESA_TURNIP)) 
    {
    	return false;
    }
    
    if (is_s3tc(format) && device->compute_bcn_auto && driverProps.driverID == VK_DRIVER_ID_SAMSUNG_PROPRIETARY)
    {
    	return false;
    }
    
	return is_rgtc(format) || is_s3tc(format) || is_bc6(format) || is_bc7(format);
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
	    },
		{
	        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	        .descriptorCount = 1
	    }
	};
	
	VkDescriptorPoolCreateInfo descpool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
	    .pNext = nullptr,
	    .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
	    .maxSets = 64,
	    .poolSizeCount = device->use_image_view ? 2u : 1u,
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
create_bcn_compute_pipelines(struct device *dev)
{
	VkResult result;
	VkLayerDispatchTable table = dev->table;
	VkDevice device = dev->handle;

	VkShaderModule s3tcShaderModule;
	VkShaderModuleCreateInfo s3tc_shader_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.codeSize = (dev->use_image_view) ? s3tc_iv_spv_len : s3tc_spv_len,
		.pCode = (dev->use_image_view) ? (const uint32_t *)s3tc_iv_spv : (const uint32_t *)s3tc_spv
	};

	VkShaderModule rgtcShaderModule;
	VkShaderModuleCreateInfo rgtc_shader_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.codeSize = (dev->use_image_view) ? rgtc_iv_spv_len : rgtc_spv_len,
		.pCode = (dev->use_image_view) ? (const uint32_t *)rgtc_iv_spv : (const uint32_t *)rgtc_spv
	};

	VkShaderModule bc6ShaderModule;
	VkShaderModuleCreateInfo bc6_shader_info = {
	    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
	    .pNext = nullptr,
	    .flags = 0,
	    .codeSize = (dev->use_image_view) ? bc6_iv_spv_len : bc6_spv_len,
	    .pCode = (dev->use_image_view) ? (const uint32_t *)bc6_iv_spv : (const uint32_t *)bc6_spv
	};

	VkShaderModule bc7ShaderModule;
	VkShaderModuleCreateInfo bc7_shader_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
	    .pNext = nullptr,
	    .flags = 0,
	    .codeSize = (dev->use_image_view) ? bc7_iv_spv_len : bc7_spv_len,
	    .pCode = (dev->use_image_view) ? (const uint32_t *)bc7_iv_spv : (const uint32_t *)bc7_spv
	};

	VkShaderModule etc2ShaderModule;
	VkShaderModuleCreateInfo etc2_shader_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
	    .pNext = nullptr,
	    .flags = 0,
	    .codeSize = etc2_encode_spv_len,
	    .pCode = (const uint32_t *) etc2_encode_spv
	};

	table.CreateShaderModule(device, &s3tc_shader_info, nullptr, &s3tcShaderModule);
	table.CreateShaderModule(device, &bc6_shader_info, nullptr, &bc6ShaderModule);
	table.CreateShaderModule(device, &bc7_shader_info, nullptr, &bc7ShaderModule);
	table.CreateShaderModule(device, &rgtc_shader_info, nullptr, &rgtcShaderModule);
	table.CreateShaderModule(device, &etc2_shader_info, nullptr, &etc2ShaderModule);

	VkPipelineShaderStageCreateInfo shader_stage_infos[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = s3tcShaderModule,
			.pName = "main",
			.pSpecializationInfo = nullptr
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = rgtcShaderModule,
			.pName = "main",
			.pSpecializationInfo = nullptr
		},
		{
		    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		    .pNext = nullptr,
		    .flags = 0,
		    .stage = VK_SHADER_STAGE_COMPUTE_BIT,                                                   
		    .module = bc6ShaderModule,                                                              
		    .pName = "main",
		    .pSpecializationInfo = nullptr                                                      
		},
		{
		    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		    .pNext = nullptr,
		    .flags = 0,
		    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
		    .module = bc7ShaderModule,
		    .pName = "main",
		    .pSpecializationInfo = nullptr
		},
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
			.descriptorType = (dev->use_image_view) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
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

	VkDescriptorSetLayoutBinding etc2_bindings[] = {
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

	VkDescriptorSetLayoutCreateInfo etc2_descriptor_set_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.bindingCount = 2,
		.pBindings = bindings
	};

	result = table.CreateDescriptorSetLayout(device,
		&etc2_descriptor_set_create_info, NULL, &dev->etc2SetLayout);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create descriptor set etc2SetLayout, res %d", result);
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
		Logger::log("error", "Failed to create pipeline layout: %d", result);
		return result;
	}

	VkPushConstantRange etc2_push_constant = {
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.offset = 0,
		.size = sizeof(struct etc2_push_constants)
	};

	VkPipelineLayoutCreateInfo etc2_layout_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.setLayoutCount = 1,
		.pSetLayouts = &dev->etc2SetLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &etc2_push_constant
	};

	result = table.CreatePipelineLayout(device,
		&etc2_layout_create_info, NULL, &dev->etc2Layout);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create pipeline etc2Layout: %d", result);
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
		{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.stage = shader_stage_infos[1],
			.layout = dev->layout,
			.basePipelineHandle = VK_NULL_HANDLE,
			.basePipelineIndex = -1
		},
		{
		    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		    .pNext = nullptr,
		    .flags = 0,
		    .stage = shader_stage_infos[2],
		    .layout = dev->layout,
		    .basePipelineHandle = VK_NULL_HANDLE,                                                   
		    .basePipelineIndex = -1                                                             
		},
		{
		    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		    .pNext = nullptr,
		    .flags = 0,
		    .stage = shader_stage_infos[3],
		    .layout = dev->layout,
		    .basePipelineHandle = VK_NULL_HANDLE,
		    .basePipelineIndex = -1
		},
		{
		    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		    .pNext = nullptr,
		    .flags = 0,
		    .stage = shader_stage_infos[4],
		    .layout = dev->etc2Layout,
		    .basePipelineHandle = VK_NULL_HANDLE,
		    .basePipelineIndex = -1
		}
	};

	VkPipeline pipelines[5];

	result = table.CreateComputePipelines(device,
		VK_NULL_HANDLE, 5, pipeline_create_info, NULL, pipelines);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create compute pipeline, res %d", result);
		return result;
	}

	dev->s3tcPipeline = pipelines[0];
	dev->rgtcPipeline = pipelines[1];
	dev->bc6Pipeline = pipelines[2];
	dev->bc7Pipeline = pipelines[3];
	dev->etc2Pipeline = pipelines[4];

	table.DestroyShaderModule(device, s3tcShaderModule, nullptr);
	table.DestroyShaderModule(device, bc6ShaderModule, nullptr);
	table.DestroyShaderModule(device, bc7ShaderModule, nullptr);
	table.DestroyShaderModule(device, rgtcShaderModule, nullptr);
	table.DestroyShaderModule(device, etc2ShaderModule, nullptr);

	result = create_new_pool(dev);

	if (result != VK_SUCCESS) {
	    Logger::log("error", "Failed to create descriptor pool, res %d", result);
		return result;
	}

	return VK_SUCCESS;
}

VkResult
encode_etc2_compute(struct device *dev,
	       		   struct command_buffer *cb,
	       		   VkFormat format,
	       		   VkBufferImageCopy *copy_region,
	       		   struct buffer *decodedBuffer,
	       		   struct buffer *stagingBuffer)
{
	VkResult result;
	VkLayerDispatchTable table;
	VkDevice device;

	table = dev->table;
	device = dev->handle;

	auto commandbuffer = cb->handle;

	uint width = copy_region->imageExtent.width;
	uint height = copy_region->imageExtent.height;

    uint32_t flags = dev->transcode_to_etc1 ? 0b00001 : 0b00000; // 0: etc2, 1: etc1
    // if (target_format == VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK) {
    //     flags |= 0b00100; // no alpha
    // }
    if (format == VK_FORMAT_BC6H_SFLOAT_BLOCK || format == VK_FORMAT_BC6H_UFLOAT_BLOCK) {
        flags |= 0b01000; // translate sfloat16 to unorm8
    }
    if (format == VK_FORMAT_BC4_SNORM_BLOCK || format == VK_FORMAT_BC5_SNORM_BLOCK) {
        flags |= 0b10000; // snorm8 to unorm8
    }

	struct etc2_push_constants constants = {
		.width = width,
		.height = height,
		.flags = flags,
	};

	VkDescriptorSet descriptorSet;
	VkDescriptorSetAllocateInfo desc_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = dev->pools.back(),
		.descriptorSetCount = 1,
		.pSetLayouts = &dev->etc2SetLayout
	};

	result = table.AllocateDescriptorSets(device,
		&desc_alloc_info, &descriptorSet);

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
		.buffer = decodedBuffer->handle,
		.offset = 0,
		.range = VK_WHOLE_SIZE
	};

	VkDescriptorBufferInfo dst_info = {
		.buffer = stagingBuffer->handle,
		.offset = 0,
		.range = VK_WHOLE_SIZE
	};

	desc_writes[0] = VkWriteDescriptorSet {
	    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet = descriptorSet,
	    .dstBinding = 0,
	    .descriptorCount = 1,
	    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	    .pBufferInfo = &src_info,
	};
	desc_writes[1] = VkWriteDescriptorSet {
	    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet = descriptorSet,
	    .dstBinding = 1,
	    .descriptorCount = 1,
	    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	    .pBufferInfo = &dst_info,
	};

	table.UpdateDescriptorSets(device, 2, desc_writes, 0, NULL);
	VkBufferMemoryBarrier bufferBarrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = stagingBuffer->handle,
        .offset = 0,
        .size = VK_WHOLE_SIZE
    };

    table.CmdPipelineBarrier(
        commandbuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, NULL,
        1, &bufferBarrier,
        0, NULL
    );
    VkPipeline etc2Pipeline = dev->etc2Pipeline;
	table.CmdBindPipeline(commandbuffer, VK_PIPELINE_BIND_POINT_COMPUTE, etc2Pipeline);
	table.CmdPushConstants(commandbuffer,
		dev->etc2Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
		sizeof(constants), &constants);
	table.CmdBindDescriptorSets(commandbuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE, dev->etc2Layout, 0, 1,
		&descriptorSet, 0, nullptr);
	table.CmdDispatch(commandbuffer, (width + 7) / 8, (height + 7) / 8, 1);
	return VK_SUCCESS;
}

VkResult
decompress_bcn_compute(struct device *dev,
		       		   VkCommandBuffer commandbuffer,
		       		   VkFormat format,
		       		   VkBufferImageCopy *copy_region,
		       		   struct buffer *srcBuffer,
		       		   struct buffer *stagingBuffer,
		       		   struct image *dstImage,
		       		   VkImageLayout dstImageLayout)
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
	        return VK_ERROR_INITIALIZATION_FAILED;
	}

	int width = copy_region->imageExtent.width;
	int height = copy_region->imageExtent.height;
	int offset = copy_region->bufferOffset;
	int bufferRowLength = copy_region->bufferRowLength;
	int offsetX = copy_region->imageOffset.x;
	int offsetY = copy_region->imageOffset.y;
	int use_image_view = dev->use_image_view;
	int use_etc2 = dev->transcode_to_etc2;

	std::unique_ptr<struct buffer> decodedBuffer;
	if (use_etc2) {
		int texel_size = is_bc6(format) ? 8 : 4;
		decodedBuffer = create_staging_buffer(dev, width * height * texel_size);
	}

	struct push_constants constants = {
		.format = format,
		.width = width,
		.height = height,
		.offset = offset,
		.bufferRowLength = bufferRowLength,
		.offsetX = offsetX,
		.offsetY = offsetY
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

	// Scope this at the UpdateDescriptorSets level to avoid use-after-free
	auto targetBuffer = use_etc2 ? decodedBuffer.get() : stagingBuffer;
	VkDescriptorBufferInfo dst_info = {
		.buffer = use_image_view ? VK_NULL_HANDLE : targetBuffer->handle,
		.offset = 0,
		.range = VK_WHOLE_SIZE
	};

	desc_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	desc_writes[0].pNext = nullptr;
	desc_writes[0].dstSet = descriptorSet;
	desc_writes[0].dstBinding = 1;
	desc_writes[0].dstArrayElement = 0;
	desc_writes[0].descriptorCount = 1;
	desc_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	desc_writes[0].pImageInfo = nullptr;
	desc_writes[0].pBufferInfo = &src_info;
	desc_writes[0].pTexelBufferView =nullptr;

	desc_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	desc_writes[1].pNext = nullptr;
	desc_writes[1].dstSet = descriptorSet;
	desc_writes[1].dstBinding = 0;
	desc_writes[1].dstArrayElement = 0;
	desc_writes[1].descriptorCount = 1;

	// Pull this out of the if block to avoid it being GC-ed (depending on compiler)
	// once out of scope, since it must live until the table.UpdateDescriptorSets
	VkDescriptorImageInfo image_info = {
		.sampler = VK_NULL_HANDLE,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL
	};
	
	if (!use_image_view) {                                
		desc_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		desc_writes[1].pImageInfo = nullptr;                                                    
		desc_writes[1].pBufferInfo = &dst_info;                                                 
		desc_writes[1].pTexelBufferView = nullptr;
	} 
	else {	
		VkComponentMapping components_mapping = {
			.r = VK_COMPONENT_SWIZZLE_IDENTITY,
			.g = VK_COMPONENT_SWIZZLE_IDENTITY,
			.b = VK_COMPONENT_SWIZZLE_IDENTITY,
			.a = VK_COMPONENT_SWIZZLE_IDENTITY
		};

		VkImageViewCreateInfo viewCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
	    	.image = dstImage->handle,
	    	.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = get_format_for_bcn(dstImage->format),
			.components = components_mapping,
	    	.subresourceRange = {
	    		.aspectMask = copy_region->imageSubresource.aspectMask,
	        	.baseMipLevel = copy_region->imageSubresource.mipLevel,
	        	.levelCount = 1,
	        	.baseArrayLayer = copy_region->imageSubresource.baseArrayLayer,
	        	.layerCount = copy_region->imageSubresource.layerCount,
	    	}
		};

		VkImageView dstImageView;
		result = table.CreateImageView(dev->handle, &viewCreateInfo, nullptr, &dstImageView);
		if (result != VK_SUCCESS) {
			Logger::log("error", "table.CreateImageView failed: result=%d", result);
			return result;
		}

		image_info.imageView = dstImageView;
		cb->currentStagingResources->AddStagingImageView(dstImageView);
		
		desc_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;                      
		desc_writes[1].pImageInfo = &image_info;                                                    
		desc_writes[1].pBufferInfo = nullptr;
		desc_writes[1].pTexelBufferView = nullptr;
	}

	table.UpdateDescriptorSets(device,
		2, desc_writes, 0, NULL);
  
    VkPipeline bcnPipeline;
    if (is_s3tc(format)) {
    	bcnPipeline = dev->s3tcPipeline;
    }
    else if (is_rgtc(format)) {
    	bcnPipeline = dev->rgtcPipeline;
    }
    else if(is_bc6(format)) {
    	bcnPipeline = dev->bc6Pipeline;
    }
    else {
    	bcnPipeline = dev->bc7Pipeline;
    }
    
	table.CmdBindPipeline(commandbuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE, bcnPipeline);

	if (use_image_view) {
		VkImageMemoryBarrier first_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = dstImage->handle,
			.subresourceRange = (VkImageSubresourceRange) {
				.aspectMask = copy_region->imageSubresource.aspectMask,
				.baseMipLevel = copy_region->imageSubresource.mipLevel,
				.levelCount = 1,
				.baseArrayLayer = copy_region->imageSubresource.baseArrayLayer,
				.layerCount = copy_region->imageSubresource.layerCount
			},
		};
		table.CmdPipelineBarrier(commandbuffer, 
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
			0, 0, nullptr, 0, nullptr, 1, &first_barrier);
	}

	table.CmdPushConstants(commandbuffer,
		dev->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
		sizeof(constants), &constants);

	table.CmdBindDescriptorSets(commandbuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE, dev->layout, 0, 1, 
		&descriptorSet, 0, nullptr);

	table.CmdDispatch(commandbuffer,
		(width + 7) / 8, (height + 7) / 8, 1);

	if (use_image_view) {
		VkImageMemoryBarrier second_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		    .pNext = nullptr,
		    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
		    .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
		    .newLayout = dstImageLayout,
		    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		    .image = dstImage->handle,
		 	.subresourceRange = (VkImageSubresourceRange) {
		    	.aspectMask = copy_region->imageSubresource.aspectMask,
		        .baseMipLevel = copy_region->imageSubresource.mipLevel,
		        .levelCount = 1,
		        .baseArrayLayer = copy_region->imageSubresource.baseArrayLayer,
		        .layerCount = copy_region->imageSubresource.layerCount
		     },
		};

		table.CmdPipelineBarrier(commandbuffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		    0, 0, nullptr, 0, nullptr, 1, &second_barrier);
	}

	if (use_etc2) {
		VkResult result = encode_etc2_compute(dev, cb, format, copy_region, decodedBuffer.get(), stagingBuffer);
		if (result != VK_SUCCESS) {
		    Logger::log("error", "encode_etc2_compute failed: %d", result);
			return result;
		}
		cb->currentStagingResources->AddStagingBuffer(std::move(decodedBuffer));
	}

	return VK_SUCCESS;
}
