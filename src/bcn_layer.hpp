#ifndef __BCN_LAYER_HPP
#define __BCN_LAYER_HPP

#include "vulkan/vk_layer.h"
#include "vk_func.hpp"
#include "logger.hpp"
#include "staging_resources.hpp"

#include <cstdint>
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

class DescriptorSetAllocator {
public:
    struct PoolSizes {
        std::vector<VkDescriptorPoolSize> sizes;
        uint32_t maxSets = 100;
    };

    explicit DescriptorSetAllocator(struct device* device, const PoolSizes& defaultSizes)
        : device(device),
          poolSizes(defaultSizes) { createNewPool(&activePool); }
    ~DescriptorSetAllocator() { cleanup(); }

    void cleanup();
    VkResult allocate(VkDescriptorSetLayout layout, VkDescriptorPool* pool, VkDescriptorSet* descriptors);
    void free(VkDescriptorPool pool, VkDescriptorSet descriptors);
    uint64_t allocatedCount() const { return allocated_count; }
private:
    VkResult createNewPool(VkDescriptorPool* descriptor_pool);

    struct device* device = nullptr;
    VkDescriptorPool activePool = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> exhaustedPools;
    PoolSizes poolSizes;
    std::mutex lock;
    std::atomic_uint64_t allocated_count = 0;
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
	VkPipeline astcPipeline;
	VkPipeline analyzeAstcDebugPipeline;
	VkPipeline watermarkPipeline;
	VkPipelineLayout layout;
	VkPipelineLayout etc2Layout;
	VkPipelineLayout astcLayout;
	VkPipelineLayout analyzeAstcLayout;
	VkQueue queue;
	uint32_t queueFamilyIndex;
	uint32_t memoryIndex;
	int use_image_view = 0;
	int transcode_to_etc1 = 0;
	int transcode_to_etc2 = 0;
	int transcode_to_astc = 0;
	int profile_transfers = 0;
	int profile_more_transfers = 0;
	int add_watermark = 0;
	int debug_astc = 0;
	int more_debug_astc = 0;
	int sample_gpu_counters = 0;
	VkDescriptorSetLayout setLayout;
	VkDescriptorSetLayout etc2SetLayout;
	VkDescriptorSetLayout astcSetLayout;
	VkDescriptorSetLayout analyzeAstcSetLayout;
	std::unique_ptr<struct buffer> lut2Buffer;
	std::unique_ptr<struct buffer> astc2pLutBuffer;
	const VkAllocationCallbacks *alloc;
	std::unique_ptr<SyncPool> syncPool;
	std::unique_ptr<DescriptorSetAllocator> descriptorSetAllocator;
	std::vector<std::unique_ptr<StagingResources>> stagingResourcesQueue;
	std::condition_variable hasCleanupWork;
	std::thread finalizer_thread;
    std::atomic_bool stop_thread {false};
    std::string dump_buffers_path;
    bool has_more_layers = false;
};

struct device *get_device(VkDevice);

#endif
