#include "staging_pool.hpp"

#include "bcn_layer.hpp"
#include "logger.hpp"

#include <algorithm>

StagingBufferPool::StagingBufferPool(struct device *dev, VkDeviceSize maxIdleBytes, Clock::duration idleTimeout)
    : dev(dev), maxIdleBytes(maxIdleBytes), idleTimeout(idleTimeout)
{
}

StagingBufferPool::~StagingBufferPool()
{
    Clear();
}

std::unique_ptr<struct buffer> StagingBufferPool::Acquire(VkDeviceSize capacity)
{
    std::lock_guard<std::mutex> guard(lock);
    auto it = idleByCapacity.find(capacity);
    if (it == idleByCapacity.end() || it->second.empty()) {
        misses++;
        return nullptr;
    }

    // Take the most recently returned buffer; the oldest ones are left for Trim().
    std::unique_ptr<struct buffer> buf = std::move(it->second.back().buf);
    it->second.pop_back();
    idleBytes -= buf->size;
    hits++;
    return buf;
}

void StagingBufferPool::Release(std::unique_ptr<struct buffer> buf)
{
    if (!buf)
        return;

    std::vector<std::unique_ptr<struct buffer>> toDestroy;
    {
        std::lock_guard<std::mutex> guard(lock);
        released++;
        if (buf->size > maxIdleBytes) {
            toDestroy.push_back(std::move(buf));
        } else {
            const VkDeviceSize bufferSize = buf->size;
            buf->stagingReuseState = StagingReuseState::NotReusable;
            idleBytes += bufferSize;
            idleByCapacity[bufferSize].push_back({ std::move(buf), Clock::now() });

            while (idleBytes > maxIdleBytes) {
                std::vector<Entry> *oldestClass = nullptr;
                for (auto& [capacity, entries] : idleByCapacity) {
                    if (!entries.empty() && (!oldestClass || entries.front().idleSince < oldestClass->front().idleSince))
                        oldestClass = &entries;
                }
                if (!oldestClass)
                    break;
                idleBytes -= oldestClass->front().buf->size;
                toDestroy.push_back(std::move(oldestClass->front().buf));
                oldestClass->erase(oldestClass->begin());
            }
        }
    }
    Destroy(toDestroy);
}

void StagingBufferPool::Trim()
{
    std::vector<std::unique_ptr<struct buffer>> toDestroy;
    {
        std::lock_guard<std::mutex> guard(lock);
        const Clock::time_point cutoff = Clock::now() - idleTimeout;
        for (auto& [capacity, entries] : idleByCapacity) {
            auto firstKept = std::find_if(entries.begin(), entries.end(),
                [cutoff](const Entry& e) { return e.idleSince >= cutoff; });
            for (auto it = entries.begin(); it != firstKept; ++it) {
                idleBytes -= it->buf->size;
                toDestroy.push_back(std::move(it->buf));
            }
            entries.erase(entries.begin(), firstKept);
        }
    }
    if (!toDestroy.empty() && dev->profile_transfers)
        Logger::log("info", "staging pool: trimmed %zu idle buffers", toDestroy.size());
    Destroy(toDestroy);
}

void StagingBufferPool::Clear()
{
    std::vector<std::unique_ptr<struct buffer>> toDestroy;
    {
        std::lock_guard<std::mutex> guard(lock);
        for (auto& [capacity, entries] : idleByCapacity) {
            for (auto& entry : entries)
                toDestroy.push_back(std::move(entry.buf));
        }
        idleByCapacity.clear();
        idleBytes = 0;
    }
    Destroy(toDestroy);
}

void StagingBufferPool::LogStats()
{
    std::lock_guard<std::mutex> guard(lock);
    Logger::log("info", "staging pool: %.1f MiB idle, %llu hits, %llu misses, %llu returned, %llu destroyed",
        idleBytes / (1024.0 * 1024.0), (unsigned long long) hits, (unsigned long long) misses,
        (unsigned long long) released, (unsigned long long) destroyed);
}

void StagingBufferPool::Destroy(std::vector<std::unique_ptr<struct buffer>>& toDestroy)
{
    for (auto& buf : toDestroy) {
        if (!buf)
            continue;
        dev->table.DestroyBuffer(dev->handle, buf->handle, buf->alloc);
        dev->table.FreeMemory(dev->handle, buf->memory, buf->alloc);
    }
    if (!toDestroy.empty()) {
        std::lock_guard<std::mutex> guard(lock);
        destroyed += toDestroy.size();
    }
    toDestroy.clear();
}
