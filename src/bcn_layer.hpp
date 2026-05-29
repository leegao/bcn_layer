#ifndef __BCN_LAYER_HPP
#define __BCN_LAYER_HPP

#include "vulkan/vk_layer.h"
#include "vk_func.hpp"
#include "logger.hpp"
#include "staging_resources.hpp"

#include <vulkan/vulkan.h>
#include <atomic>
#include <unistd.h>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <memory>
#include <cstring>
#include <thread>
#include <condition_variable>

#undef VK_LAYER_EXPORT
#if defined(WIN32)
#define VK_LAYER_EXPORT extern "C" __declspec(dllexport)
#else
#define VK_LAYER_EXPORT extern "C"
#endif

#define VK_DRIVER_ID_QUALCOMM_PROPRIETARY 8                                  
#define VK_DRIVER_ID_ARM_PROPRIETARY 9                                       
#define VK_DRIVER_ID_MESA_TURNIP 18                                         
#define VK_DRIVER_ID_SAMSUNG_PROPRIETARY 21

template <typename T>
void* GetKey(T item) {
    return *(void**) item;
}

extern std::mutex global_lock;
typedef std::lock_guard<std::mutex> scoped_lock;

// Object pool for pairs of semaphores and fences for staging resources
// cleanup signaling
class SyncPool {
public:
    explicit SyncPool(VkDevice device) : device(device) {}
    ~SyncPool();

    std::pair<VkSemaphore, VkFence> Acquire();

    void Release(VkSemaphore sem, VkFence fence) {
        freeSemaphores.push_back(sem);
        freeFences.push_back(fence);
    }

private:
    VkDevice device;
    std::vector<VkFence> freeFences;
    std::vector<VkSemaphore> freeSemaphores;
};

struct device {
	VkDevice handle;
	VkPhysicalDevice physical;
	VkPhysicalDeviceProperties2 props2;
	VkPhysicalDeviceFeatures features;
	VkPhysicalDeviceDriverProperties driverProps;
	bool compute_bcn_auto;
	VkLayerDispatchTable table;
	VkPipeline s3tcPipeline;
	VkPipeline rgtcPipeline;
	VkPipeline bc6Pipeline;
	VkPipeline bc7Pipeline;
	VkPipeline etc2Pipeline;
	VkPipelineLayout layout;
	VkPipelineLayout etc2Layout;
	VkQueue queue;
	uint32_t memoryIndex;
	int use_image_view = 0;
	int transcode_to_etc1 = 0;
	int transcode_to_etc2 = 0;
	VkDescriptorSetLayout setLayout;
	VkDescriptorSetLayout etc2SetLayout;
	std::vector<VkDescriptorPool> pools;
	const VkAllocationCallbacks *alloc;
	std::unique_ptr<SyncPool> syncPool;
	std::vector<std::unique_ptr<StagingResources>> stagingResourcesQueue;
	std::condition_variable hasCleanupWork;
	std::thread finalizer_thread;
    std::atomic_bool stop_thread {false};
};

struct device *get_device(VkDevice);

#endif
