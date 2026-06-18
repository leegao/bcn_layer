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
#include "astc_encoder_spv.h"
#include "watermark_spv.h"
#include "lut2.h"
#include "astc_2p_lut_s2.h"
#include <cstdint>

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
		    return VK_FORMAT_R8G8B8A8_SRGB;
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
    // TODO: opportunistically use RGB8
    switch (format) {
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
		case VK_FORMAT_BC2_SRGB_BLOCK:
		case VK_FORMAT_BC3_SRGB_BLOCK:
		case VK_FORMAT_BC7_SRGB_BLOCK:
		    return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
		case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
		case VK_FORMAT_BC2_UNORM_BLOCK:
		case VK_FORMAT_BC3_UNORM_BLOCK:
		case VK_FORMAT_BC4_UNORM_BLOCK:
		case VK_FORMAT_BC5_UNORM_BLOCK:
		case VK_FORMAT_BC7_UNORM_BLOCK:
		    return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
		case VK_FORMAT_BC4_SNORM_BLOCK:
		case VK_FORMAT_BC5_SNORM_BLOCK:
			return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
		default:
			return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
	}
}

VkFormat get_format_for_bcn_to_astc(struct device *device, VkFormat format) {
    // TODO: opportunistically use RGB8
    switch (format) {
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
		case VK_FORMAT_BC2_SRGB_BLOCK:
		case VK_FORMAT_BC3_SRGB_BLOCK:
		case VK_FORMAT_BC7_SRGB_BLOCK:
		    return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
		case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
		case VK_FORMAT_BC2_UNORM_BLOCK:
		case VK_FORMAT_BC3_UNORM_BLOCK:
		case VK_FORMAT_BC4_UNORM_BLOCK:
		case VK_FORMAT_BC5_UNORM_BLOCK:
		case VK_FORMAT_BC7_UNORM_BLOCK:
		    return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
		case VK_FORMAT_BC4_SNORM_BLOCK:
		case VK_FORMAT_BC5_SNORM_BLOCK:
			return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
		default:
			return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
	}
}

std::string vk_format_to_string(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:       return "BC1";
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:        return "BC1s";
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:      return "BC1a";
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:       return "BC1sa";
        case VK_FORMAT_BC2_UNORM_BLOCK:           return "BC2";
        case VK_FORMAT_BC2_SRGB_BLOCK:            return "BC2s";
        case VK_FORMAT_BC3_UNORM_BLOCK:           return "BC3";
        case VK_FORMAT_BC3_SRGB_BLOCK:            return "BC3s";
        case VK_FORMAT_BC4_UNORM_BLOCK:           return "BC4";
        case VK_FORMAT_BC4_SNORM_BLOCK:           return "BC4s";
        case VK_FORMAT_BC5_UNORM_BLOCK:           return "BC5";
        case VK_FORMAT_BC5_SNORM_BLOCK:           return "BC5s";
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:         return "BC6";
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:         return "BC6s";
        case VK_FORMAT_BC7_UNORM_BLOCK:           return "BC7";
        case VK_FORMAT_BC7_SRGB_BLOCK:            return "BC7s";
        case VK_FORMAT_R8G8B8A8_UNORM:            return "RGBA";
        case VK_FORMAT_R8G8B8A8_SRGB:             return "sRGBA";
        case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK: return "ETC2";
        case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:  return "ETC2s";
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:      return "ASTC";
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:       return "ASTCs";
        default: return "VK_FMT_" + std::to_string(static_cast<int>(format));
    }
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

void upload_static_lut(struct device *dev, struct buffer* lut_buf, const uint32_t* data, size_t size_bytes) {
    if (!lut_buf || lut_buf->handle == VK_NULL_HANDLE || lut_buf->memory == VK_NULL_HANDLE) {
        Logger::log("error", "upload_static_lut: Staging buffer allocation or memory backing failed.");
        return;
    }

    void* mapped = nullptr;
    VkResult result = dev->table.MapMemory(dev->handle, lut_buf->memory, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (result != VK_SUCCESS || !mapped) {
        Logger::log("error", "upload_static_lut: vkMapMemory failed with error code %d", result);
        return;
    }
    
    std::memcpy(mapped, data, size_bytes);
    dev->table.UnmapMemory(dev->handle, lut_buf->memory);
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

	VkShaderModule astcShaderModule;
    VkShaderModuleCreateInfo astc_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = astc_encoder_spv_len,
        .pCode = (const uint32_t *) astc_encoder_spv,
    };

   	VkShaderModule watermarkShaderModule;
    VkShaderModuleCreateInfo watermark_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = watermark_spv_len,
        .pCode = (const uint32_t *) watermark_spv,
    };

	table.CreateShaderModule(device, &s3tc_shader_info, nullptr, &s3tcShaderModule);
	table.CreateShaderModule(device, &bc6_shader_info, nullptr, &bc6ShaderModule);
	table.CreateShaderModule(device, &bc7_shader_info, nullptr, &bc7ShaderModule);
	table.CreateShaderModule(device, &rgtc_shader_info, nullptr, &rgtcShaderModule);
	table.CreateShaderModule(device, &etc2_shader_info, nullptr, &etc2ShaderModule);
	table.CreateShaderModule(device, &astc_shader_info, nullptr, &astcShaderModule);
	table.CreateShaderModule(device, &watermark_shader_info, nullptr, &watermarkShaderModule);

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
		{
		    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		    .pNext = nullptr,
		    .flags = 0,
		    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
		    .module = astcShaderModule,
		    .pName = "main",
		    .pSpecializationInfo = nullptr
		},
		{
		    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		    .pNext = nullptr,
		    .flags = 0,
		    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
		    .module = watermarkShaderModule,
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

	VkDescriptorSetLayoutBinding astc_bindings[] = {
        { 
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        { 
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        { 
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        { 
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        }
    };

    VkDescriptorSetLayoutCreateInfo astc_layout_desc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 4,
        .pBindings = astc_bindings
    };
    table.CreateDescriptorSetLayout(device, &astc_layout_desc, nullptr, &dev->astcSetLayout);

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

	VkPushConstantRange astc_push = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(struct astc_push_constants)
    };

    VkPipelineLayoutCreateInfo astc_pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &dev->astcSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &astc_push
    };
    table.CreatePipelineLayout(device, &astc_pipeline_layout_info, nullptr, &dev->astcLayout);

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
		},
		{
		    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		    .pNext = nullptr,
		    .flags = 0,
		    .stage = shader_stage_infos[5],
		    .layout = dev->astcLayout,
		    .basePipelineHandle = VK_NULL_HANDLE,
		    .basePipelineIndex = -1
		},
		{
		    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		    .pNext = nullptr,
		    .flags = 0,
		    .stage = shader_stage_infos[6],
		    .layout = dev->etc2Layout, // TODO(leegao): same layout for now
		    .basePipelineHandle = VK_NULL_HANDLE,
		    .basePipelineIndex = -1
		}
	};

	VkPipeline pipelines[7];

	result = table.CreateComputePipelines(device,
		VK_NULL_HANDLE, 7, pipeline_create_info, NULL, pipelines);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create compute pipeline, res %d", result);
		return result;
	}

	dev->s3tcPipeline = pipelines[0];
	dev->rgtcPipeline = pipelines[1];
	dev->bc6Pipeline = pipelines[2];
	dev->bc7Pipeline = pipelines[3];
	dev->etc2Pipeline = pipelines[4];
	dev->astcPipeline = pipelines[5];
	dev->watermarkPipeline = pipelines[6];

	table.DestroyShaderModule(device, s3tcShaderModule, nullptr);
	table.DestroyShaderModule(device, bc6ShaderModule, nullptr);
	table.DestroyShaderModule(device, bc7ShaderModule, nullptr);
	table.DestroyShaderModule(device, rgtcShaderModule, nullptr);
	table.DestroyShaderModule(device, etc2ShaderModule, nullptr);
	table.DestroyShaderModule(device, astcShaderModule, nullptr);
	table.DestroyShaderModule(device, watermarkShaderModule, nullptr);

	if (dev->transcode_to_astc) {
		dev->lut2Buffer = create_staging_buffer(dev, lut2_bin_len * sizeof(uint32_t), VK_FORMAT_UNDEFINED, 0, 0);
		dev->astc2pLutBuffer = create_staging_buffer(dev, astc_2p_lut_s2_bin_len * sizeof(uint32_t), VK_FORMAT_UNDEFINED, 0, 0);

		upload_static_lut(
            dev, dev->lut2Buffer.get(), (const uint32_t *) lut2_bin, lut2_bin_len);
		upload_static_lut(
            dev, dev->astc2pLutBuffer.get(), (const uint32_t *) astc_2p_lut_s2_bin,
            astc_2p_lut_s2_bin_len);
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

	VkDescriptorPool pool;
	VkDescriptorSet descriptorSet;
	result = dev->descriptorSetAllocator->allocate(dev->etc2SetLayout, &pool, &descriptorSet);
	if (result != VK_SUCCESS) {
	    Logger::log("error", "Failed to allocate descriptor set: %d", result);
		return result;
	}
	cb->currentStagingResources->AddDescriptorSet(pool, descriptorSet);

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

	VkWriteDescriptorSet desc_writes[2];
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
        .buffer = decodedBuffer->handle,
        .offset = 0,
        .size = VK_WHOLE_SIZE
    };

    table.CmdPipelineBarrier(
        commandbuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
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

	VkQueryPool queryPool = VK_NULL_HANDLE;
    std::pair<uint32_t, uint32_t> query = { UINT32_MAX, UINT32_MAX };
    if (dev->profile_transfers) {
        query = cb->currentStagingResources->AllocateQueryPair(
            commandbuffer, "encode_etc2", format, width * height, queryPool);
    }
    if (query.first != UINT32_MAX) {
        table.CmdWriteTimestamp(commandbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, query.first);
    }
    table.CmdDispatch(commandbuffer, (width + 7) / 8, (height + 7) / 8, 1);
	if (query.first != UINT32_MAX) {
	    table.CmdWriteTimestamp(commandbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, query.second);
	}

	return VK_SUCCESS;
}

VkResult
encode_astc_compute(struct device *dev,
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

	// TODO(leegao): more optimization flags (e.g. 2-partition mode, fast mode, etc)
    uint32_t flags = 0b00000;
    if (format == VK_FORMAT_BC6H_SFLOAT_BLOCK || format == VK_FORMAT_BC6H_UFLOAT_BLOCK) {
        flags |= 0b01000; // translate sfloat16 to unorm8
    }
    if (format == VK_FORMAT_BC4_SNORM_BLOCK || format == VK_FORMAT_BC5_SNORM_BLOCK) {
        flags |= 0b10000; // snorm8 to unorm8
    }

	struct astc_push_constants constants = {
		.width = width,
		.height = height,
		.flags = flags,
	};

	VkDescriptorPool pool;
	VkDescriptorSet descriptorSet;
	result = dev->descriptorSetAllocator->allocate(dev->astcSetLayout, &pool, &descriptorSet);
	if (result != VK_SUCCESS) {
	    Logger::log("error", "Failed to allocate descriptor set: %d", result);
		return result;
	}
	cb->currentStagingResources->AddDescriptorSet(pool, descriptorSet);

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

	VkDescriptorBufferInfo lut2_info = {
		.buffer = dev->lut2Buffer->handle,
		.offset = 0,
		.range = VK_WHOLE_SIZE
	};
	
	VkDescriptorBufferInfo astc_2p_lut_info = {
		.buffer = dev->astc2pLutBuffer->handle,
		.offset = 0,
		.range = VK_WHOLE_SIZE
	};

	VkWriteDescriptorSet desc_writes[4];
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
	desc_writes[2] = VkWriteDescriptorSet {
	    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet = descriptorSet,
	    .dstBinding = 2,
	    .descriptorCount = 1,
	    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	    .pBufferInfo = &lut2_info,
	};
	desc_writes[3] = VkWriteDescriptorSet {
	    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet = descriptorSet,
	    .dstBinding = 3,
	    .descriptorCount = 1,
	    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	    .pBufferInfo = &astc_2p_lut_info,
	};

	table.UpdateDescriptorSets(device, 4, desc_writes, 0, NULL);
	VkBufferMemoryBarrier bufferBarrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = decodedBuffer->handle,
        .offset = 0,
        .size = VK_WHOLE_SIZE
    };

    table.CmdPipelineBarrier(
        commandbuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, NULL,
        1, &bufferBarrier,
        0, NULL
    );

	table.CmdBindPipeline(commandbuffer, VK_PIPELINE_BIND_POINT_COMPUTE, dev->astcPipeline);
	table.CmdPushConstants(commandbuffer,
		dev->astcLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
		sizeof(constants), &constants);
	table.CmdBindDescriptorSets(commandbuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE, dev->astcLayout, 0, 1,
		&descriptorSet, 0, nullptr);

	VkQueryPool queryPool = VK_NULL_HANDLE;
    std::pair<uint32_t, uint32_t> query = { UINT32_MAX, UINT32_MAX };
    if (dev->profile_transfers) {
        query = cb->currentStagingResources->AllocateQueryPair(
            commandbuffer, "encode_astc", format, width * height, queryPool);
    }
    if (query.first != UINT32_MAX) {
        table.CmdWriteTimestamp(commandbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, query.first);
    }
    table.CmdDispatch(commandbuffer, (width + 7) / 8, (height + 7) / 8, 1);
	if (query.first != UINT32_MAX) {
	    table.CmdWriteTimestamp(commandbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, query.second);
	}

	return VK_SUCCESS;
}

VkResult
add_debug_watermark(struct device *dev,
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
    uint32_t flags = (decodedBuffer->id & 0xFFFFu) | ((uint) format << 16);

    // Reuse etc2 layout and constants for watermarking
	struct etc2_push_constants constants = {
		.width = width,
		.height = height,
		.flags = flags,
	};
   
	VkDescriptorPool pool;
	VkDescriptorSet descriptorSet;
	result = dev->descriptorSetAllocator->allocate(dev->etc2SetLayout, &pool, &descriptorSet);
	if (result != VK_SUCCESS) {
	    Logger::log("error", "Failed to allocate descriptor set for add_debug_watermark: %d", result);
		return result;
	}
	cb->currentStagingResources->AddDescriptorSet(pool, descriptorSet);
   
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
   
	VkWriteDescriptorSet desc_writes[2];
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
           .buffer = decodedBuffer->handle,
           .offset = 0,
           .size = VK_WHOLE_SIZE
       };
   
    table.CmdPipelineBarrier(
        commandbuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, NULL,
        1, &bufferBarrier,
        0, NULL
    );

	table.CmdBindPipeline(commandbuffer, VK_PIPELINE_BIND_POINT_COMPUTE, dev->watermarkPipeline);
	table.CmdPushConstants(commandbuffer,
		dev->etc2Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
		sizeof(constants), &constants);
	table.CmdBindDescriptorSets(commandbuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE, dev->etc2Layout, 0, 1,
		&descriptorSet, 0, nullptr);
   
	VkQueryPool queryPool = VK_NULL_HANDLE;
    std::pair<uint32_t, uint32_t> query = { UINT32_MAX, UINT32_MAX };
    if (dev->profile_transfers) {
        query = cb->currentStagingResources->AllocateQueryPair(
            commandbuffer, "watermarking", format, width * height, queryPool);
    }
    if (query.first != UINT32_MAX) {
        table.CmdWriteTimestamp(commandbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, query.first);
    }
    table.CmdDispatch(commandbuffer, (width + 7) / 8, (height + 7) / 8, 1);
	if (query.first != UINT32_MAX) {
	    table.CmdWriteTimestamp(commandbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, query.second);
	}
   
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
	int depth = copy_region->imageExtent.depth;
	int offset = copy_region->bufferOffset;
	int bufferRowLength = copy_region->bufferRowLength;
	int bufferImageHeight = copy_region->bufferImageHeight;
	int offsetX = copy_region->imageOffset.x;
	int offsetY = copy_region->imageOffset.y;
	int use_image_view = dev->use_image_view && depth == 1;
	int use_etc2 = dstImage->transcode_to_etc2;
	int use_astc = dstImage->transcode_to_astc;
	// TODO: add support for rgba16_sfloat
	int add_watermark = dev->add_watermark && depth == 1 && get_format_for_bcn(format) != VK_FORMAT_R16G16B16A16_SFLOAT;

	std::unique_ptr<struct buffer> decodedBuffer;
	if (use_etc2 || use_astc) {
		int texel_size = is_bc6(format) ? 8 : 4;
		decodedBuffer = create_staging_buffer(dev, width * height * depth * texel_size, get_format_for_bcn(format), width, height);
	}

	std::unique_ptr<struct buffer> waterMarkedDecodedBuffer;
	if (add_watermark) {
    	int texel_size = is_bc6(format) ? 8 : 4;
    	waterMarkedDecodedBuffer = create_staging_buffer(dev, width * height * depth * texel_size, get_format_for_bcn(format), width, height);
	}

	struct push_constants constants = {
		.format = format,
		.width = width,
		.height = height,
		.depth = depth,
		.offset = offset,
		.bufferRowLength = bufferRowLength,
		.bufferImageHeight = bufferImageHeight,
		.offsetX = offsetX,
		.offsetY = offsetY
	};

	VkDescriptorPool pool;
	VkDescriptorSet descriptorSet;
	result = dev->descriptorSetAllocator->allocate(dev->setLayout, &pool, &descriptorSet);
	if (result != VK_SUCCESS) {
	    Logger::log("error", "Failed to allocate descriptor set: %d", result);
		return result;
	}
	cb->currentStagingResources->AddDescriptorSet(pool, descriptorSet);

	VkWriteDescriptorSet desc_writes[2];
	
	VkDescriptorBufferInfo src_info = {
		.buffer = srcBuffer->handle,
		.offset = static_cast<VkDeviceSize>(offset),
		.range = VK_WHOLE_SIZE
	};

	// Scope this at the UpdateDescriptorSets level to avoid use-after-free
	// [srcBuffer -> stagingBuffer] if no transcode and no watermark
	// [srcBuffer -> waterMarkedDecodedBuffer] -> stagingBuffer if no transcode and yes watermark
	// [srcBuffer -> decodedBuffer] -> stagingBuffer if yes transcode and no watermark
	// [srcBuffer -> decodedBuffer] -> waterMarkedDecodedBuffer -> stagingBuffer if yes transcode and yes watermark
	auto targetBuffer = stagingBuffer;
	if (use_etc2 || use_astc) {
	    targetBuffer = decodedBuffer.get(); // unwatermarked buffer
	} else if (add_watermark) {
        targetBuffer = waterMarkedDecodedBuffer.get(); // watermarked buffer
	}
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
	} else {
        VkBufferMemoryBarrier first_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = srcBuffer->handle,
            .offset = 0,
            .size = VK_WHOLE_SIZE
        };
        table.CmdPipelineBarrier(commandbuffer, 
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
            0, 0, nullptr, 1, &first_barrier, 0, nullptr);
	}

	table.CmdPushConstants(commandbuffer,
		dev->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
		sizeof(constants), &constants);

	table.CmdBindDescriptorSets(commandbuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE, dev->layout, 0, 1, 
		&descriptorSet, 0, nullptr);

	VkQueryPool queryPool = VK_NULL_HANDLE;
    std::pair<uint32_t, uint32_t> query = { UINT32_MAX, UINT32_MAX };
    if (dev->profile_transfers) {
        query = cb->currentStagingResources->AllocateQueryPair(
            commandbuffer, "decompress_bcn", format, width * height * depth, queryPool);
    }
    if (query.first != UINT32_MAX) {
        table.CmdWriteTimestamp(commandbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, query.first);
    }
	table.CmdDispatch(commandbuffer, (width + 7) / 8, (height + 7) / 8, depth);
	if (query.first != UINT32_MAX) {
	    table.CmdWriteTimestamp(commandbuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, query.second);
	}

	if (add_watermark) {
    	// srcBuffer -> [waterMarkedDecodedBuffer -> stagingBuffer] if no transcode and yes watermark
    	// srcBuffer -> [decodedBuffer -> waterMarkedDecodedBuffer] -> stagingBuffer if yes transcode
    	auto targetBufferWatermark = stagingBuffer;
    	if (use_etc2 || use_astc) {
    	    targetBufferWatermark = waterMarkedDecodedBuffer.get();
    	}
	    VkResult result = add_debug_watermark(dev, cb, format, copy_region, targetBuffer, targetBufferWatermark);
		if (result != VK_SUCCESS) {
		    Logger::log("error", "add_debug_watermark failed: %d", result);
			return result;
		}
	}

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
	
	// srcBuffer -> [decodedBuffer -> stagingBuffer] if yes transcode and no watermark
	// srcBuffer -> decodedBuffer -> [waterMarkedDecodedBuffer -> stagingBuffer] if yes transcode and yes watermark
	auto sourceDecodedBuffer = add_watermark ? waterMarkedDecodedBuffer.get() : decodedBuffer.get();
	if (use_etc2) {
		VkResult result = encode_etc2_compute(dev, cb, format, copy_region, sourceDecodedBuffer, stagingBuffer);
		if (result != VK_SUCCESS) {
		    Logger::log("error", "encode_etc2_compute failed: %d", result);
			return result;
		}
	} else if (use_astc) {
		VkResult result = encode_astc_compute(dev, cb, format, copy_region, sourceDecodedBuffer, stagingBuffer);
		if (result != VK_SUCCESS) {
		    Logger::log("error", "encode_astc_compute failed: %d", result);
			return result;
		}
	}

	if (use_etc2 || use_astc) {
	    cb->currentStagingResources->AddStagingBuffer(std::move(decodedBuffer));
	}

	if (add_watermark) {
	    cb->currentStagingResources->AddStagingBuffer(std::move(waterMarkedDecodedBuffer));
	}

	return VK_SUCCESS;
}
