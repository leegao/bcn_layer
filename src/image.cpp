#include "image.hpp"
#include <vector>

std::unordered_map<VkImage, std::unique_ptr<struct image>> imagesMap;

struct image *
find_image(VkImage image) {
	auto it = imagesMap.find(image);

	if (it == imagesMap.end())
		return nullptr;

	return it->second.get();
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_CreateImage(VkDevice device,
					 const VkImageCreateInfo *pCreateInfo,
					 const VkAllocationCallbacks *pAllocator,
					 VkImage *pImage)
{
	VkResult result;
	VkLayerDispatchTable table;
	VkImageCreateInfo create_info = *pCreateInfo;

	struct device *dev = get_device(device);
	if (!dev)
		return VK_ERROR_INITIALIZATION_FAILED;

	table = dev->table;
	bool transcode_to_etc2 = false;
	bool transcode_to_astc = false;
	std::vector<VkFormat> compatibleFormats; // in-place modification of VkImageFormatListCreateInfo seems to make dxvk not happy
	if (is_supported_bcn_format(dev, pCreateInfo->format)) {
	    // TODO: Texture3D not supported for recompression at the moment
	    transcode_to_etc2 = dev->transcode_to_etc2 && pCreateInfo->imageType != VK_IMAGE_TYPE_3D;
		transcode_to_astc = !transcode_to_etc2 
		                    && dev->transcode_to_astc && pCreateInfo->imageType != VK_IMAGE_TYPE_3D;
	    auto target_format = get_format_for_bcn(pCreateInfo->format);
		if (transcode_to_etc2)
			target_format = get_format_for_bcn_to_etc2(dev, pCreateInfo->format);
		else if (transcode_to_astc)
			target_format = get_format_for_bcn_to_astc(dev, pCreateInfo->format);
	    create_info.format = target_format;
		if (!transcode_to_etc2 && !transcode_to_astc)
	        create_info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;

		// https://docs.vulkan.org/spec/latest/chapters/resources.html#VUID-VkImageCreateInfo-flags-04738
	    // create_info.flags &= ~VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

        // err:   vkCreateImage(): 
        //     pCreateInfo->pNext<VkImageFormatListCreateInfo>.pViewFormats[0] (VK_FORMAT_BC3_UNORM_BLOCK) and 
        //     VkImageCreateInfo::format (VK_FORMAT_R8G8B8A8_UNORM) are not class compatible.
        const VkBaseInStructure* pnext = reinterpret_cast<const VkBaseInStructure*>(create_info.pNext);
        while (pnext != nullptr) {
            switch (static_cast<int32_t>(pnext->sType)) {
                case VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO: {
                    auto* ext = reinterpret_cast<VkImageFormatListCreateInfo*>(const_cast<VkBaseInStructure*>(pnext));
                    if (ext->pViewFormats != nullptr) {
                        auto* formats = const_cast<VkFormat*>(ext->pViewFormats);
                        for (int i = 0; i < ext->viewFormatCount; i++) {
                            auto original_view_format = formats[i];
                            auto target_view_format = get_format_for_bcn(original_view_format);
                      		if (transcode_to_etc2)
                     			target_view_format = get_format_for_bcn_to_etc2(dev, original_view_format);
                      		else if (transcode_to_astc)
                     			target_view_format = get_format_for_bcn_to_astc(dev, original_view_format);
                            compatibleFormats.push_back(target_view_format);
                        }
                        ext->pViewFormats = compatibleFormats.data();
                    }
                    break;
                }
                default:
                    break;
            }
            pnext = pnext->pNext;
        }					
	}

	result = table.CreateImage(device, &create_info, pAllocator, pImage);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create image, res %d", result);
		return result;
	}

    auto image = std::make_unique<struct image>();
    image->handle = *pImage,
    image->format = pCreateInfo->format;
    image->device = dev;
    image->alloc = pAllocator;
    image->transcode_to_etc2 = transcode_to_etc2;
    image->transcode_to_astc = transcode_to_astc;

    {
    	scoped_lock l(global_lock);
    	imagesMap[*pImage] = std::move(image);
    }

	return VK_SUCCESS;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_CreateImageView(VkDevice device,
						 const VkImageViewCreateInfo *pCreateInfo,
						 const VkAllocationCallbacks *pAllocator,
						 VkImageView *pImageView)
{
	VkResult result;
	VkLayerDispatchTable table;
	VkImageViewCreateInfo create_info = *pCreateInfo;

	struct device *dev = get_device(device);
	if (!dev)
		return VK_ERROR_INITIALIZATION_FAILED;

	table = dev->table;

	if (is_supported_bcn_format(dev, pCreateInfo->format)) {
		create_info.format = get_format_for_bcn(pCreateInfo->format);
		struct image *img = find_image(pCreateInfo->image);
		if (img && img->transcode_to_etc2)
			create_info.format = get_format_for_bcn_to_etc2(dev, pCreateInfo->format);
		else if (img && img->transcode_to_astc)
			create_info.format = get_format_for_bcn_to_astc(dev, pCreateInfo->format);
	}

	result = table.CreateImageView(device, &create_info, pAllocator, pImageView);
	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create image view, res %d", result);
		return result;
	}

	return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_DestroyImage(VkDevice device,
					  VkImage image,
					  const VkAllocationCallbacks *pAllocator)
{
	scoped_lock l(global_lock);

	struct device *dev = get_device(device);
	struct image *img = find_image(image);
	if (!dev || !img)
		return;

	dev->table.DestroyImage(device, image, pAllocator);	
	imagesMap.erase(image);
}
