#include "fence.hpp"

std::unordered_map<VkFence, std::shared_ptr<struct fence>> fencesMap;

struct fence *
get_fence(VkFence fence) 
{
	auto it = fencesMap.find(fence);

	if (it == fencesMap.end())
		return nullptr;

	return it->second.get();
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_CreateFence(VkDevice device,
					 const VkFenceCreateInfo *pCreateInfo,
					 const VkAllocationCallbacks *pAllocator,
					 VkFence *pFence)
{
	VkResult result;

	struct device *dev = get_device(device);
	if (!dev)
		return VK_ERROR_INITIALIZATION_FAILED;

	result = dev->table.CreateFence(device, pCreateInfo, pAllocator, pFence);
	if (result != VK_SUCCESS)
		return result;

	auto fence = std::make_shared<struct fence>();
	fence->handle = *pFence;
	fence->device = dev;
	dev->alloc = pAllocator;

	{
		scoped_lock l(global_lock);
		fencesMap[*pFence] = fence;
	}

	return VK_SUCCESS;
	
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_WaitForFences(VkDevice device,
					   uint32_t fenceCount,
					   const VkFence *pFences,
					   VkBool32 waitAll,
					   uint64_t timeout)
{
	VkResult result;

	struct device *dev = get_device(device);
	if (!dev)
		return VK_ERROR_INITIALIZATION_FAILED;

	return dev->table.WaitForFences(device, fenceCount, pFences, waitAll, timeout);
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_DestroyFence(VkDevice device,
					  VkFence fence,
					  const VkAllocationCallbacks *pAllocator)
{
	scoped_lock l(global_lock);

	struct device *dev = get_device(device);
	if (!dev)
		return;

	if (fence != VK_NULL_HANDLE)
		dev->table.DestroyFence(device, fence, pAllocator);
		
	fencesMap.erase(fence);
}
