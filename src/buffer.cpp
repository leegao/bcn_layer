#include "buffer.hpp"

#include "bcn_layer.hpp"
#include <atomic>

std::unordered_map<VkBuffer, std::unique_ptr<struct buffer>> buffersMap;
std::atomic<int> bufferIdCounter;

std::unique_ptr<struct buffer>
create_staging_buffer(struct device *dev, int size, VkFormat format, int width, int height, std::string_view label)
{
	VkResult result;
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkLayerDispatchTable table = dev->table;
	VkDevice device = dev->handle;
	uint align = 15;
	if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB) {
	    align = 63;
	} else if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
	    align = 127;
	}

	// [info]: buffer handle: 0xd2a0000000d2a, size: 64, mem size: 64, fmt: 97, width: 2, height: 2
	// The descriptor buffer (VkBuffer 0xd2a0000000d2a) size is 64 bytes, 64 bytes were bound,
	// and the highest out of bounds access was at [79] bytes
	size = (size + align) & ~align;
	VkBufferCreateInfo buffer_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.size = static_cast<VkDeviceSize>(size),
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = nullptr
	};

	result = table.CreateBuffer(device, &buffer_create_info, nullptr, &buffer);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create staging buffer, res %d", result);
		return NULL;
	}
	VkMemoryRequirements mem_reqs;
    table.GetBufferMemoryRequirements(device, buffer, &mem_reqs);
    VkMemoryAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = dev->memoryIndex
    };


	result = table.AllocateMemory(device, &allocate_info, nullptr, &memory);
	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to allocate staging buffer memory, res %d", result);
		return NULL;
	}

	result = table.BindBufferMemory(device, buffer, memory, 0);
	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to bind staging buffer memory, res %d", result);
		return NULL;
	}

	int id = bufferIdCounter.fetch_add(1);
	auto staging_buf = std::make_unique<struct buffer>();
	staging_buf->handle = buffer;
	staging_buf->memory = memory;
	staging_buf->offset = 0;
	staging_buf->device = dev;
	staging_buf->alloc = nullptr;
	staging_buf->size = size;
	staging_buf->format = format;
	staging_buf->label = label;
	staging_buf->width = width;
	staging_buf->height = height;
	staging_buf->id = id;

	return staging_buf;
}

struct buffer *
find_buffer(VkBuffer buffer)
{
	auto it = buffersMap.find(buffer);

	if (it == buffersMap.end())
		return nullptr;

	return it->second.get();
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_CreateBuffer(VkDevice device,
					  const VkBufferCreateInfo *pCreateInfo,
					  const VkAllocationCallbacks *pAllocator,
					  VkBuffer *pBuffer)
{
	VkResult result;
	VkLayerDispatchTable table;
	VkBufferCreateInfo create_info = *pCreateInfo;

	struct device *dev = get_device(device);
	if (!dev)
		return VK_ERROR_INITIALIZATION_FAILED;

	table = dev->table;

	create_info.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	result = table.CreateBuffer(device, &create_info, pAllocator, pBuffer);
	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create buffer, res %d", result);
		return result;
	}

	auto buf = std::make_unique<struct buffer>();
	buf->handle = *pBuffer;
	buf->size = pCreateInfo->size;
	buf->device = dev;
	buf->alloc = pAllocator;
	buf->format = VK_FORMAT_UNDEFINED;
	buf->id = 0;

	{
		scoped_lock l(global_lock);
		buffersMap[*pBuffer] = std::move(buf);
	}

	return VK_SUCCESS;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_BindBufferMemory(VkDevice device,
						  VkBuffer buffer,
						  VkDeviceMemory memory,
						  VkDeviceSize memoryOffset)
{
	VkResult result;
	VkLayerDispatchTable table;

	scoped_lock l(global_lock);

	struct device *dev = get_device(device);
	if (!dev)
		return VK_ERROR_INITIALIZATION_FAILED;

	table = dev->table;

	result = table.BindBufferMemory(device, buffer, memory, memoryOffset);
	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to bind buffer memory, res %d", result);
		return result;
	}

	struct buffer *buf = find_buffer(buffer);
	buf->memory = memory;
	buf->offset = memoryOffset;

	return VK_SUCCESS;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_BindBufferMemory2(VkDevice device,
						  uint32_t bindInfoCount,
						  const VkBindBufferMemoryInfo* pBindInfos)
{
	VkResult result;
	VkLayerDispatchTable table;

	scoped_lock l(global_lock);

	struct device *dev = get_device(device);
	if (!dev)
		return VK_ERROR_INITIALIZATION_FAILED;

	table = dev->table;

	// Don't emulate with BindBufferMemory in case pBindInfos has a pNext
	result = table.BindBufferMemory2(device, bindInfoCount, pBindInfos);
	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to bind buffer memory, res %d", result);
		return result;
	}

	for (uint32_t i = 0; i < bindInfoCount; i++) {
		struct buffer *buf = find_buffer(pBindInfos[i].buffer);
		if (buf) {
			buf->memory = pBindInfos[i].memory;
			buf->offset = pBindInfos[i].memoryOffset;
		}
	}

	return VK_SUCCESS;
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_DestroyBuffer(VkDevice device,
					   VkBuffer buffer,
					   const VkAllocationCallbacks *pAllocator)
{
	scoped_lock l(global_lock);

	struct device *dev = get_device(device);
	struct buffer *buf = find_buffer(buffer);
	if (!dev || !buf)
		return;

	dev->table.DestroyBuffer(device, buffer, pAllocator);
	buffersMap.erase(buffer);
}
