#ifndef __STAGING_POOL_HPP
#define __STAGING_POOL_HPP

#include "buffer.hpp"

#include <vulkan/vulkan.h>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

struct device;

// A buffer only enters the pool once the GPU has finished with it, so taking one needs no
// barrier. It's called from several threads, hence the mutex.
class StagingBufferPool {
public:
    using Clock = std::chrono::steady_clock;

    StagingBufferPool(struct device *dev, VkDeviceSize maxIdleBytes, Clock::duration idleTimeout);
    ~StagingBufferPool();

    StagingBufferPool(const StagingBufferPool&) = delete;
    StagingBufferPool& operator=(const StagingBufferPool&) = delete;

    std::unique_ptr<struct buffer> Acquire(VkDeviceSize capacity);
    void Release(std::unique_ptr<struct buffer> buf);
    void Trim();
    // Must be called before the VkDevice is destroyed.
    void Clear();
    void LogStats();

private:
    struct Entry {
        std::unique_ptr<struct buffer> buf;
        Clock::time_point idleSince;
    };

    void Destroy(std::vector<std::unique_ptr<struct buffer>>& toDestroy);

    struct device *dev;
    const VkDeviceSize maxIdleBytes;
    const Clock::duration idleTimeout;

    std::mutex lock;
    std::map<VkDeviceSize, std::vector<Entry>> idleByCapacity;  // oldest first
    VkDeviceSize idleBytes = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t released = 0;
    uint64_t destroyed = 0;
};

#endif
