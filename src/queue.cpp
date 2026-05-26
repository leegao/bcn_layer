#include "queue.hpp"
#include "command_buffer.hpp"

std::unordered_map<VkQueue, std::shared_ptr<struct queue>> queuesMap;

struct queue *
get_queue(VkQueue queue) {
	auto it = queuesMap.find(queue);
	if (it == queuesMap.end())
		return nullptr;

	return it->second.get();
}

VK_LAYER_EXPORT void VKAPI_CALL
BCnLayer_GetDeviceQueue(VkDevice device,
						uint32_t queueFamilyIndex,
						uint32_t queueIndex,
						VkQueue *pQueue)
{
	scoped_lock l(global_lock);
	
	struct device *dev = get_device(device);
	dev->table.GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);

	auto queue = std::make_shared<struct queue>();
	queue->handle = *pQueue;
	queue->device = dev;

	queuesMap[*pQueue] = queue;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_QueueSubmit(VkQueue queue,
					 uint32_t submitInfoCount,
					 const VkSubmitInfo *pSubmitInfos,
					 VkFence fence)
{
	scoped_lock l(global_lock);

	struct queue *q = get_queue(queue);
	struct fence *f = get_fence(fence);

	for (uint32_t i = 0; i < submitInfoCount; i++) {
		VkSubmitInfo submit_info = pSubmitInfos[i];
		for (uint32_t j = 0; j < submit_info.commandBufferCount; j++) {
			struct command_buffer *cb = get_command_buffer(submit_info.pCommandBuffers[j]);
			cb->fence = f;
		}
	}

	return q->device->table.QueueSubmit(queue, submitInfoCount, pSubmitInfos, fence);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL
BCnLayer_QueueSubmit2(VkQueue queue,
					 uint32_t submitInfoCount,
					 const VkSubmitInfo2 *pSubmitInfos,
					 VkFence fence)
{
	scoped_lock l(global_lock);

	struct queue *q = get_queue(queue);
	struct fence *f = get_fence(fence);

	for (uint32_t i = 0; i < submitInfoCount; i++) {
		VkSubmitInfo2 submit_info = pSubmitInfos[i];
		for (uint32_t j = 0; j < submit_info.commandBufferInfoCount; j++) {
			struct command_buffer *cb = get_command_buffer(submit_info.pCommandBufferInfos[j].commandBuffer);
			cb->fence = f;
		}
	}

	return q->device->table.QueueSubmit2(queue, submitInfoCount, pSubmitInfos, fence);
}
