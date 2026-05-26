#include "bcn_layer.hpp"
#include "bcn.hpp"
#include "vulkan/vk_layer.h"

#include <unistd.h>
#include <cmath>

std::unordered_map<void *, VkLayerInstanceDispatchTable> instanceDispatch;
std::unordered_map<void *, VkInstance> instanceMap;
std::unordered_map<void *, VkPhysicalDeviceFeatures> featuresMap;
std::unordered_map<void *, VkPhysicalDeviceProperties2> propertiesMap;
std::unordered_map<void *, VkPhysicalDeviceDriverProperties> driverPropertiesMap;
std::unordered_map<void *, std::shared_ptr<struct device>> deviceMap;

bool bcn_compute_auto = false;

std::mutex global_lock;

#define GETPROCADDR(func) \
if (!strcmp(pName, "vk" #func)) \
	return (PFN_vkVoidFunction)&BCnLayer_##func;

struct device *
get_device(VkDevice device)
{
    auto it = deviceMap.find(GetKey(device));
    
	if (it == deviceMap.end())
		return nullptr;
	
	return it->second.get();
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
						const VkAllocationCallbacks *pAllocator,
						VkInstance *pInstance)
{
	VkLayerInstanceCreateInfo *layerCreateInfo = (VkLayerInstanceCreateInfo *)pCreateInfo->pNext;
	VkResult result;
	
    while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO |
    						   layerCreateInfo->function != VK_LAYER_LINK_INFO)) 
    {
    	layerCreateInfo = (VkLayerInstanceCreateInfo *)layerCreateInfo->pNext;
    }

    if (!layerCreateInfo)
        return VK_ERROR_INITIALIZATION_FAILED;

    
    PFN_vkGetInstanceProcAddr gip = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
	layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

    PFN_vkCreateInstance createInstance = (PFN_vkCreateInstance)gip(VK_NULL_HANDLE, "vkCreateInstance");
    result = createInstance(pCreateInfo, pAllocator, pInstance);

    if (result != VK_SUCCESS) {
    	Logger::log("error", "Failed to create instance, res %d", result);
    	return result;
    }

    bcn_compute_auto = getenv("BCN_COMPUTE_AUTO") && atoi(getenv("BCN_COMPUTE_AUTO"));

    VkLayerInstanceDispatchTable table;
    table.GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)gip(*pInstance, "vkGetInstanceProcAddr");
    table.DestroyInstance = (PFN_vkDestroyInstance)gip(*pInstance, "vkDestroyInstance");
    table.EnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)gip(*pInstance, "vkEnumeratePhysicalDevices");
    table.GetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)gip(*pInstance, "vkGetPhysicalDeviceMemoryProperties");
    table.GetPhysicalDeviceFormatProperties = (PFN_vkGetPhysicalDeviceFormatProperties)gip(*pInstance, "vkGetPhysicalDeviceFormatProperties");
    table.GetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)gip(*pInstance, "vkGetPhysicalDeviceProperties");
    table.GetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)gip(*pInstance, "vkGetPhysicalDeviceProperties2");
    table.GetPhysicalDeviceImageFormatProperties = (PFN_vkGetPhysicalDeviceImageFormatProperties)gip(*pInstance, "vkGetPhysicalDeviceImageFormatProperties");
    table.GetPhysicalDeviceImageFormatProperties2 = (PFN_vkGetPhysicalDeviceImageFormatProperties2)gip(*pInstance, "vkGetPhysicalDeviceImageFormatProperties2");
    table.GetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures)gip(*pInstance, "vkGetPhysicalDeviceFeatures");
    table.GetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)gip(*pInstance, "vkGetPhysicalDeviceFeatures2");
    table.GetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gip(*pInstance, "vkGetPhysicalDeviceQueueFamilyProperties");

    {
    	scoped_lock l(global_lock);
    	instanceDispatch[GetKey(*pInstance)] = table;
    	instanceMap[GetKey(*pInstance)] = *pInstance;	
    }

    return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_DestroyInstance(VkInstance instance, 
						 const VkAllocationCallbacks *pAllocator)
{
	scoped_lock l(global_lock);
	if (!instance)
		return;
		
	VkLayerInstanceDispatchTable table = instanceDispatch[GetKey(instance)];
	table.DestroyInstance(instance, pAllocator);
	instanceMap.erase(GetKey(instance));
	instanceDispatch.erase(GetKey(instance));
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_EnumeratePhysicalDevices(VkInstance instance,
								  uint32_t *pPhysicalDeviceCount,
								  VkPhysicalDevice *pPhysicalDevices)
{
	scoped_lock l(global_lock);

	VkResult result;

	result = instanceDispatch[GetKey(instance)].EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);

	if (result != VK_SUCCESS || *pPhysicalDeviceCount < 1 || pPhysicalDevices == nullptr)
		return result;

	for (uint32_t index = 0; index < *pPhysicalDeviceCount; index++) {
		VkPhysicalDeviceFeatures features{};
		instanceDispatch[GetKey(instance)].GetPhysicalDeviceFeatures(pPhysicalDevices[index], &features);

		VkPhysicalDeviceDriverProperties driverProperties{};
		VkPhysicalDeviceProperties2 props2{};
		driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
		props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		props2.pNext = &driverProperties;
		instanceDispatch[GetKey(instance)].GetPhysicalDeviceProperties2(pPhysicalDevices[index], &props2);

		featuresMap[GetKey(pPhysicalDevices[index])] = features;
		propertiesMap[GetKey(pPhysicalDevices[index])] = props2;
		driverPropertiesMap[GetKey(pPhysicalDevices[index])] = driverProperties;
	}
	
	return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
								   VkPhysicalDeviceFeatures *pFeatures)
{
	scoped_lock l(global_lock);

	instanceDispatch[GetKey(physicalDevice)].GetPhysicalDeviceFeatures(physicalDevice, pFeatures);
	pFeatures->textureCompressionBC = true;
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                    VkPhysicalDeviceFeatures2 *pFeatures)
{
    scoped_lock l(global_lock);

    instanceDispatch[GetKey(physicalDevice)].GetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
    pFeatures->features.textureCompressionBC = true;
}

VKAPI_ATTR VkResult VKAPI_CALL
BCnLayer_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice,
                                                VkFormat format,
                                                VkImageType type,
                                                VkImageTiling tiling,
                                                VkImageUsageFlags usage,
                                                VkImageCreateFlags flags,
                                                VkImageFormatProperties *pImageFormatProperties)
{
	scoped_lock l(global_lock);

	VkPhysicalDeviceProperties2 props2 = propertiesMap[GetKey(physicalDevice)];
	VkPhysicalDeviceDriverProperties driverProps = driverPropertiesMap[GetKey(physicalDevice)];
	
	switch(format) {
    	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
   		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
   		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
   		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
   		case VK_FORMAT_BC2_UNORM_BLOCK:
   		case VK_FORMAT_BC2_SRGB_BLOCK:
   		case VK_FORMAT_BC3_UNORM_BLOCK:
   		case VK_FORMAT_BC3_SRGB_BLOCK:
   			if (bcn_compute_auto && driverProps.driverID == VK_DRIVER_ID_SAMSUNG_PROPRIETARY)
   				break;
   		case VK_FORMAT_BC4_UNORM_BLOCK:
   		case VK_FORMAT_BC4_SNORM_BLOCK:
   		case VK_FORMAT_BC5_UNORM_BLOCK:
   		case VK_FORMAT_BC5_SNORM_BLOCK:
   		case VK_FORMAT_BC6H_UFLOAT_BLOCK:
   		case VK_FORMAT_BC6H_SFLOAT_BLOCK:
   		case VK_FORMAT_BC7_UNORM_BLOCK:
   		case VK_FORMAT_BC7_SRGB_BLOCK:
   		    if (bcn_compute_auto && ((driverProps.driverID == VK_DRIVER_ID_QUALCOMM_PROPRIETARY && props2.properties.driverVersion > VK_MAKE_VERSION(512, 530, 0)) ||
   		                             driverProps.driverID == VK_DRIVER_ID_MESA_TURNIP)) 
   		    {
   		    	break;
   		    }
   		      
   			if (type & VK_IMAGE_TYPE_1D) {
   				pImageFormatProperties->maxExtent.width = props2.properties.limits.maxImageDimension1D;
   			    pImageFormatProperties->maxExtent.height = 1;
   			    pImageFormatProperties->maxExtent.depth = 1;
   			}
   			if (type & VK_IMAGE_TYPE_2D) {
   				if (flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) {
   			    	pImageFormatProperties->maxExtent.width = props2.properties.limits.maxImageDimensionCube;
   			        pImageFormatProperties->maxExtent.height = props2.properties.limits.maxImageDimensionCube;
   			    }
   			    else {
   			    	pImageFormatProperties->maxExtent.width = props2.properties.limits.maxImageDimension2D;
   			        pImageFormatProperties->maxExtent.height = props2.properties.limits.maxImageDimension2D;
   			    }
   			    pImageFormatProperties->maxExtent.depth = 1;
   			}
   			if (type & VK_IMAGE_TYPE_3D) {
   			         pImageFormatProperties->maxExtent.width = props2.properties.limits.maxImageDimension3D;
   			         pImageFormatProperties->maxExtent.height = props2.properties.limits.maxImageDimension3D;
   			         pImageFormatProperties->maxExtent.depth = props2.properties.limits.maxImageDimension3D;
   			}
   			if (tiling & VK_IMAGE_TILING_LINEAR ||
   			    tiling & VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT ||
   			    flags & VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT)
   				pImageFormatProperties->maxMipLevels = 1;
   			else
   				pImageFormatProperties->maxMipLevels = log2(pImageFormatProperties->maxExtent.width > pImageFormatProperties->maxExtent.height ? pImageFormatProperties->maxExtent.width :  pImageFormatProperties->maxExtent.height);
   			
   			if (tiling & VK_IMAGE_TILING_LINEAR ||
   			    ((tiling & VK_IMAGE_TILING_OPTIMAL) && type & VK_IMAGE_TYPE_3D))
   				pImageFormatProperties->maxArrayLayers = 1;
   			else
   			    pImageFormatProperties->maxArrayLayers = props2.properties.limits.maxImageArrayLayers;
   			    
   			pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   			pImageFormatProperties->maxResourceSize = 562949953421312;
        	return VK_SUCCESS;
        default:
            break;
   }

   return instanceDispatch[GetKey(physicalDevice)].GetPhysicalDeviceImageFormatProperties(physicalDevice,
      format, type, tiling, usage, flags, pImageFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
BCnLayer_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice,
                                                 const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
                                                 VkImageFormatProperties2* pImageFormatProperties)
{
	scoped_lock l(global_lock);

	VkPhysicalDeviceProperties2 props2 = propertiesMap[GetKey(physicalDevice)];
	VkPhysicalDeviceDriverProperties driverProps = driverPropertiesMap[GetKey(physicalDevice)];
	
    switch(pImageFormatInfo->format) {
		case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
   		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
   		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
   		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
   		case VK_FORMAT_BC2_UNORM_BLOCK:
   		case VK_FORMAT_BC2_SRGB_BLOCK:
   		case VK_FORMAT_BC3_UNORM_BLOCK:
   		case VK_FORMAT_BC3_SRGB_BLOCK:
   			if (bcn_compute_auto && driverProps.driverID == VK_DRIVER_ID_SAMSUNG_PROPRIETARY)
   				break;
   		case VK_FORMAT_BC4_UNORM_BLOCK:
   		case VK_FORMAT_BC4_SNORM_BLOCK:
   		case VK_FORMAT_BC5_UNORM_BLOCK:
   		case VK_FORMAT_BC5_SNORM_BLOCK:
   		case VK_FORMAT_BC6H_UFLOAT_BLOCK:
   		case VK_FORMAT_BC6H_SFLOAT_BLOCK:
   		case VK_FORMAT_BC7_UNORM_BLOCK:
   		case VK_FORMAT_BC7_SRGB_BLOCK:
   			if (bcn_compute_auto && ((driverProps.driverID == VK_DRIVER_ID_QUALCOMM_PROPRIETARY && props2.properties.driverVersion > VK_MAKE_VERSION(512, 530, 0)) ||
   			                         driverProps.driverID == VK_DRIVER_ID_MESA_TURNIP)) 
   			{
   				break;
   			}
   				
   			if (pImageFormatInfo->type & VK_IMAGE_TYPE_1D) {
   				pImageFormatProperties->imageFormatProperties.maxExtent.width = props2.properties.limits.maxImageDimension1D;
   				pImageFormatProperties->imageFormatProperties.maxExtent.height = 1;
   				pImageFormatProperties->imageFormatProperties.maxExtent.depth = 1;
   			}
   			if (pImageFormatInfo->type & VK_IMAGE_TYPE_2D) {
   				if (pImageFormatInfo->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) {
   			    	pImageFormatProperties->imageFormatProperties.maxExtent.width = props2.properties.limits.maxImageDimensionCube;
   			        pImageFormatProperties->imageFormatProperties.maxExtent.height = props2.properties.limits.maxImageDimensionCube;
   			    }
   			    else {
   			        pImageFormatProperties->imageFormatProperties.maxExtent.width = props2.properties.limits.maxImageDimension2D;
   			        pImageFormatProperties->imageFormatProperties.maxExtent.height = props2.properties.limits.maxImageDimension2D;
   			    }
   			    pImageFormatProperties->imageFormatProperties.maxExtent.depth = 1;
   			}
   			if (pImageFormatInfo->type & VK_IMAGE_TYPE_3D) {
   				pImageFormatProperties->imageFormatProperties.maxExtent.width = props2.properties.limits.maxImageDimension3D;
   			    pImageFormatProperties->imageFormatProperties.maxExtent.height = props2.properties.limits.maxImageDimension3D;
   			    pImageFormatProperties->imageFormatProperties.maxExtent.depth = props2.properties.limits.maxImageDimension3D;
   			}
   			if (pImageFormatInfo->tiling & VK_IMAGE_TILING_LINEAR ||
   			    pImageFormatInfo->tiling & VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT ||
   			    pImageFormatInfo->flags & VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT)
   				pImageFormatProperties->imageFormatProperties.maxMipLevels = 1;
   			else
   			    pImageFormatProperties->imageFormatProperties.maxMipLevels = log2(pImageFormatProperties->imageFormatProperties.maxExtent.width > pImageFormatProperties->imageFormatProperties.maxExtent.height ? pImageFormatProperties->imageFormatProperties.maxExtent.width :  pImageFormatProperties->imageFormatProperties.maxExtent.height);
   			
   			if (pImageFormatInfo->tiling & VK_IMAGE_TILING_LINEAR ||
   			    ((pImageFormatInfo->tiling & VK_IMAGE_TILING_OPTIMAL) && pImageFormatInfo->type & VK_IMAGE_TYPE_3D))
   				pImageFormatProperties->imageFormatProperties.maxArrayLayers = 1;
   			else
   				pImageFormatProperties->imageFormatProperties.maxArrayLayers = props2.properties.limits.maxImageArrayLayers;
   				
   			pImageFormatProperties->imageFormatProperties.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   			pImageFormatProperties->imageFormatProperties.maxResourceSize = 562949953421312;
      		return VK_SUCCESS;
   		default:
      		break;
   	}

   return instanceDispatch[GetKey(physicalDevice)].GetPhysicalDeviceImageFormatProperties2(physicalDevice,
      pImageFormatInfo, pImageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL
BCnLayer_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice,
                                          VkFormat format,
                                          VkFormatProperties* pFormatProperties)
{
	scoped_lock l(global_lock);

	instanceDispatch[GetKey(physicalDevice)].GetPhysicalDeviceFormatProperties(physicalDevice, 
		format, pFormatProperties);
	                                  
	VkPhysicalDeviceProperties2 props2 = propertiesMap[GetKey(physicalDevice)];
    VkPhysicalDeviceDriverProperties driverProps = driverPropertiesMap[GetKey(physicalDevice)];
      
    switch (format) {
    	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
    	case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    	case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
   		case VK_FORMAT_BC2_SRGB_BLOCK:
   		case VK_FORMAT_BC3_UNORM_BLOCK:
   		case VK_FORMAT_BC3_SRGB_BLOCK:
   			if (bcn_compute_auto && driverProps.driverID == VK_DRIVER_ID_SAMSUNG_PROPRIETARY)
   				break;
   		case VK_FORMAT_BC4_UNORM_BLOCK:
   		case VK_FORMAT_BC4_SNORM_BLOCK:
   		case VK_FORMAT_BC5_UNORM_BLOCK:
   		case VK_FORMAT_BC5_SNORM_BLOCK:
   		case VK_FORMAT_BC6H_UFLOAT_BLOCK:
   		case VK_FORMAT_BC6H_SFLOAT_BLOCK:
   		case VK_FORMAT_BC7_UNORM_BLOCK:
   		case VK_FORMAT_BC7_SRGB_BLOCK:
   			if (bcn_compute_auto && ((driverProps.driverID == VK_DRIVER_ID_QUALCOMM_PROPRIETARY && props2.properties.driverVersion > VK_MAKE_VERSION(512, 530, 0)) ||
   			                         driverProps.driverID == VK_DRIVER_ID_MESA_TURNIP)) 
   			{
   				break;
   			}
   			                                        
   			pFormatProperties->optimalTilingFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
   			return;
   		default:
   			break;
   }
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_CreateDevice(VkPhysicalDevice physicalDevice,
					  const VkDeviceCreateInfo *pCreateInfo,
					  const VkAllocationCallbacks *pAllocator,
					  VkDevice *pDevice)
{
	VkResult result;
	VkLayerDeviceCreateInfo *layerCreateInfo = (VkLayerDeviceCreateInfo *)pCreateInfo->pNext;
	VkDeviceCreateInfo createInfo = *pCreateInfo;

	while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
							   layerCreateInfo->function != VK_LAYER_LINK_INFO))
	{
		layerCreateInfo = (VkLayerDeviceCreateInfo *)layerCreateInfo->pNext;
	}

	if (layerCreateInfo == NULL) {
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
	layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

    VkInstance instance = instanceMap[GetKey(physicalDevice)];
    if (instance == VK_NULL_HANDLE)
    	return VK_ERROR_INITIALIZATION_FAILED;

    VkPhysicalDeviceMemoryProperties memoryProps{};
    uint32_t idx;
    uint32_t memoryIndex;

    instanceDispatch[GetKey(instance)].GetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProps);
    for (idx = 0; idx < memoryProps.memoryTypeCount; idx++) {
    	if (memoryProps.memoryTypes[idx].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        	break;
    }

    memoryIndex = idx < memoryProps.memoryTypeCount ? idx : UINT32_MAX;

    if (createInfo.pEnabledFeatures) {
    	VkPhysicalDeviceFeatures enabledFeatures = *createInfo.pEnabledFeatures;
    	enabledFeatures.textureCompressionBC &= featuresMap[GetKey(physicalDevice)].textureCompressionBC;
    	createInfo.pEnabledFeatures = &enabledFeatures;
    }

    PFN_vkCreateDevice createDevice = (PFN_vkCreateDevice)gipa(instance, "vkCreateDevice");
    result = createDevice(physicalDevice, &createInfo, pAllocator, pDevice);

    if (result != VK_SUCCESS) {
    	Logger::log("error", "Failed to create device, res %d", result);
    	return result;
    }

    VkLayerDispatchTable table;
    table.GetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)gdpa(*pDevice, "vkGetDeviceProcAddr");
    table.DestroyDevice = (PFN_vkDestroyDevice)gdpa(*pDevice, "vkDestroyDevice");
    table.AllocateMemory = (PFN_vkAllocateMemory)gdpa(*pDevice, "vkAllocateMemory");
    table.FreeMemory = (PFN_vkFreeMemory)gdpa(*pDevice, "vkFreeMemory");
    table.CreateImage = (PFN_vkCreateImage)gdpa(*pDevice, "vkCreateImage");
    table.CreateImageView = (PFN_vkCreateImageView)gdpa(*pDevice, "vkCreateImageView");
    table.DestroyImage = (PFN_vkDestroyImage)gdpa(*pDevice, "vkDestroyImage");
    table.CreateBuffer = (PFN_vkCreateBuffer)gdpa(*pDevice, "vkCreateBuffer");
    table.BindBufferMemory = (PFN_vkBindBufferMemory)gdpa(*pDevice, "vkBindBufferMemory");
    table.DestroyBuffer = (PFN_vkDestroyBuffer)gdpa(*pDevice, "vkDestroyBuffer");
    table.AllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)gdpa(*pDevice, "vkAllocateCommandBuffers");
    table.CreateCommandPool = (PFN_vkCreateCommandPool)gdpa(*pDevice, "vkCreateCommandPool");
    table.GetDeviceQueue = (PFN_vkGetDeviceQueue)gdpa(*pDevice, "vkGetDeviceQueue");
    table.CreateFence = (PFN_vkCreateFence)gdpa(*pDevice, "vkCreateFence");
    table.DestroyFence = (PFN_vkDestroyFence)gdpa(*pDevice, "vkDestroyFence");
    table.WaitForFences = (PFN_vkWaitForFences)gdpa(*pDevice, "vkWaitForFences");
    table.DeviceWaitIdle = (PFN_vkDeviceWaitIdle)gdpa(*pDevice, "vkDeviceWaitIdle");
    table.BeginCommandBuffer = (PFN_vkBeginCommandBuffer)gdpa(*pDevice, "vkBeginCommandBuffer");
    table.ResetCommandBuffer = (PFN_vkResetCommandBuffer)gdpa(*pDevice, "vkResetCommandBuffer");
    table.EndCommandBuffer = (PFN_vkEndCommandBuffer)gdpa(*pDevice, "vkEndCommandBuffer");
    table.QueueSubmit = (PFN_vkQueueSubmit)gdpa(*pDevice, "vkQueueSubmit");
    table.QueueSubmit2 = (PFN_vkQueueSubmit2)gdpa(*pDevice, "vkQueueSubmit2");
    table.FreeCommandBuffers = (PFN_vkFreeCommandBuffers)gdpa(*pDevice, "vkFreeCommandBuffers");
    table.CreateDescriptorSetLayout = (PFN_vkCreateDescriptorSetLayout)gdpa(*pDevice, "vkCreateDescriptorSetLayout");
    table.CreateShaderModule = (PFN_vkCreateShaderModule)gdpa(*pDevice, "vkCreateShaderModule");
    table.CreatePipelineLayout = (PFN_vkCreatePipelineLayout)gdpa(*pDevice, "vkCreatePipelineLayout");
    table.CreateComputePipelines = (PFN_vkCreateComputePipelines)gdpa(*pDevice, "vkCreateComputePipelines");
    table.CreateDescriptorPool = (PFN_vkCreateDescriptorPool)gdpa(*pDevice, "vkCreateDescriptorPool");
    table.AllocateDescriptorSets = (PFN_vkAllocateDescriptorSets)gdpa(*pDevice, "vkAllocateDescriptorSets");
    table.UpdateDescriptorSets = (PFN_vkUpdateDescriptorSets)gdpa(*pDevice, "vkUpdateDescriptorSets");
    table.CmdBindPipeline = (PFN_vkCmdBindPipeline)gdpa(*pDevice, "vkCmdBindPipeline");
    table.CmdPushConstants = (PFN_vkCmdPushConstants)gdpa(*pDevice, "vkCmdPushConstants");
    table.CmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)gdpa(*pDevice, "vkCmdBindDescriptorSets");
    table.CmdDispatch = (PFN_vkCmdDispatch)gdpa(*pDevice, "vkCmdDispatch");
    table.CmdCopyBufferToImage = (PFN_vkCmdCopyBufferToImage)gdpa(*pDevice, "vkCmdCopyBufferToImage");
    table.CmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)gdpa(*pDevice, "vkCmdPipelineBarrier");
    table.DestroyDescriptorPool = (PFN_vkDestroyDescriptorPool)gdpa(*pDevice, "vkDestroyDescriptorPool");
    table.DestroyDescriptorSetLayout = (PFN_vkDestroyDescriptorSetLayout)gdpa(*pDevice, "vkDestroyDescriptorSetLayout");
    table.DestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)gdpa(*pDevice, "vkDestroyPipelineLayout");
    table.DestroyPipeline = (PFN_vkDestroyPipeline)gdpa(*pDevice, "vkDestroyPipeline");
    table.DestroyShaderModule = (PFN_vkDestroyShaderModule)gdpa(*pDevice, "vkDestroyShaderModule");

    uint32_t queueCount;
    VkQueue queue;

   	std::vector<VkQueueFamilyProperties> queueProps;
    instanceDispatch[GetKey(instance)].GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, nullptr);
    queueProps.resize(queueCount);
    instanceDispatch[GetKey(instance)].GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queueProps.data());

    uint32_t i = 0;
    for (const auto& family : queueProps) {
    	if (family.queueFlags & VK_QUEUE_COMPUTE_BIT)
        	break;

        i++;
    }

    table.GetDeviceQueue(*pDevice, i, 0, &queue);

    auto device = std::make_shared<struct device>();
    device->handle = *pDevice;
    device->physical = physicalDevice;
    device->props2 = propertiesMap[GetKey(physicalDevice)];
    device->driverProps = driverPropertiesMap[GetKey(physicalDevice)];
    device->features = featuresMap[GetKey(physicalDevice)];
    device->compute_bcn_auto = bcn_compute_auto;
    device->table = table;
    device->memoryIndex = memoryIndex;
    device->queue = queue;
    device->alloc = pAllocator;
    device->use_image_view = getenv("BCN_COMPUTE_IMAGE_VIEW") ? atoi(getenv("BCN_COMPUTE_IMAGE_VIEW")) : 1;
   
    result = create_bcn_compute_pipelines(device.get());
    if (result != VK_SUCCESS) {
    	Logger::log("error", "Failed to create BCn compute pipeline, res %d", result);
        return result;
    }
   
	{
		scoped_lock l(global_lock);
    	deviceMap[GetKey(*pDevice)] = device;
    }

	return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_DestroyDevice(VkDevice device, 
					   const VkAllocationCallbacks *pAllocator)
{
	scoped_lock l(global_lock);

	struct device *dev = get_device(device);
	if (!dev)
		return;
		
	dev->table.DeviceWaitIdle(device);

	for (const auto& pool : dev->pools)
		dev->table.DestroyDescriptorPool(device, pool, nullptr);
			
	dev->pools.clear();
	dev->table.DestroyDescriptorSetLayout(device, dev->setLayout, nullptr);
	dev->table.DestroyPipelineLayout(device, dev->layout, nullptr);
	dev->table.DestroyPipeline(device, dev->s3tcPipeline, nullptr);
	dev->table.DestroyPipeline(device, dev->bc7Pipeline, nullptr);
	dev->table.DestroyPipeline(device, dev->bc6Pipeline, nullptr);
	dev->table.DestroyPipeline(device, dev->rgtcPipeline, nullptr);
	if (device != VK_NULL_HANDLE)
		dev->table.DestroyDevice(device, pAllocator);
				
	deviceMap.erase(GetKey(device));
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
BCnLayer_GetDeviceProcAddr(VkDevice device, 
						   const char *pName)
{
	GETPROCADDR(CreateImage);
	GETPROCADDR(CreateImageView);
	GETPROCADDR(DestroyDevice);
	GETPROCADDR(DestroyImage);
	GETPROCADDR(CreateBuffer);
	GETPROCADDR(BindBufferMemory);
	GETPROCADDR(DestroyBuffer);
	GETPROCADDR(AllocateCommandBuffers);
	GETPROCADDR(FreeCommandBuffers);
	GETPROCADDR(CmdCopyBufferToImage);
	GETPROCADDR(GetDeviceQueue);
	GETPROCADDR(QueueSubmit);
	GETPROCADDR(CreateFence);
	GETPROCADDR(DestroyFence);
	GETPROCADDR(WaitForFences);

	{
		scoped_lock l(global_lock);
		struct device *dev = get_device(device);
		if (!dev)
		    return NULL;

		return dev->table.GetDeviceProcAddr(device, pName);
	}
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
BCnLayer_GetInstanceProcAddr(VkInstance instance, 
							 const char *pName)
{   
	GETPROCADDR(CreateInstance);
	GETPROCADDR(EnumeratePhysicalDevices)
	GETPROCADDR(GetPhysicalDeviceFeatures);
	GETPROCADDR(GetPhysicalDeviceFormatProperties);
	GETPROCADDR(GetPhysicalDeviceImageFormatProperties);
	GETPROCADDR(GetPhysicalDeviceImageFormatProperties2);
	GETPROCADDR(GetPhysicalDeviceFeatures2);
	GETPROCADDR(DestroyInstance);
	GETPROCADDR(CreateDevice);

	{
		scoped_lock l(global_lock);
		VkLayerInstanceDispatchTable table = instanceDispatch[GetKey(instance)];
		return table.GetInstanceProcAddr(instance, pName);
	}
}
