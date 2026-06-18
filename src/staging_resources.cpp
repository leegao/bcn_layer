#include "staging_resources.hpp"

#include "bcn.hpp"
#include "bcn_layer.hpp"
#include "buffer.hpp"
#include <cstdint>
#include <fstream>
#include <sstream>
#include <map>

std::pair<VkSemaphore, VkFence> StagingResources::MakeFence() {
    auto *dev = get_device(device);
    if (!dev || completed != VK_NULL_HANDLE)
        return {semaphore, completed};

    if (IsEmpty())
        return {semaphore, completed};

    auto [sem, fence] = dev->syncPool->Acquire();
    completed = fence;
    semaphore = sem;
    return {semaphore, completed};
}

void StagingResources::WaitForCompletion() {
    if (has_completed) return;
    auto *dev = get_device(device);
    if (!dev) return;
    dev->table.WaitForFences(device, 1, &completed, VK_TRUE, UINT64_MAX);
    has_completed = true;
}

std::pair<uint32_t, uint32_t> StagingResources::AllocateQueryPair(
    VkCommandBuffer cmdBuf,
    const std::string& label,
    VkFormat format,
    uint64_t texture_size,
    VkQueryPool& outPool) {
    auto *dev = get_device(device);
    if (!dev) return { UINT32_MAX, UINT32_MAX };
    if (queryPools.empty() || queryPools.back().allocatedQueries + 2 > kPoolBlockSize) {
        // Allocate a new pool
        VkQueryPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = kPoolBlockSize,
            .pipelineStatistics = 0,
        };
        VkQueryPool newPool = VK_NULL_HANDLE;
        VkResult res = dev->table.CreateQueryPool(device, &poolInfo, nullptr, &newPool);
        if (res != VK_SUCCESS) {
            Logger::log("error", "Failed to allocate query pool block: %d", res);
            return { UINT32_MAX, UINT32_MAX };
        }
        dev->table.CmdResetQueryPool(cmdBuf, newPool, 0, kPoolBlockSize);
        queryPools.push_back({ newPool, 0 });
    }

    auto& activeBlock = queryPools.back();
    uint32_t startId = activeBlock.allocatedQueries;
    activeBlock.allocatedQueries += 2;

    outPool = activeBlock.handle;
    size_t activePoolIdx = queryPools.size() - 1;

    trackedQueries.push_back({ label, format, texture_size, activePoolIdx, startId, startId + 1 });
    return { startId, startId + 1 };
}

void StagingResources::Cleanup() {
    if (freed) return;
    if (completed == VK_NULL_HANDLE) return;
    if (semaphore == VK_NULL_HANDLE) return;
    
    freed = true;

    auto *dev = get_device(device);
    if (!dev) return;

    if (completed != VK_NULL_HANDLE && semaphore != VK_NULL_HANDLE) {
        {
            scoped_lock l(global_lock);
            dev->syncPool->Release(semaphore, completed);
        }
        completed = VK_NULL_HANDLE;
        semaphore = VK_NULL_HANDLE;
    }

    if (dev->profile_transfers) {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();
        auto uncompressed_size = MemoryUsage(VK_FORMAT_R8G8B8A8_UNORM) + MemoryUsage(VK_FORMAT_R16G16B16A16_SFLOAT);
        auto total_size = MemoryUsage();
        double memory_usage_mb = ((double)(total_size - uncompressed_size)) / 1024 / 1024;
        Logger::log("info", "Cleaning up batch %d with %d buffers (%d MB raw, %d MB recompressed), draw took %d ms, throughput = %0.2f MB/s", 
            id, Size(), uncompressed_size / 1024 / 1024, (total_size - uncompressed_size) / 1024 / 1024, timestamp - this->timestamp, 
            memory_usage_mb / (timestamp - this->timestamp) * 1000);
    }

    if (dev->profile_transfers && !queryPools.empty() && !trackedQueries.empty()) {
        std::vector<std::vector<uint64_t>> allPoolResults(queryPools.size());
        bool success = true;

        for (size_t i = 0; i < queryPools.size(); ++i) {
            auto count = queryPools[i].allocatedQueries;
            allPoolResults[i].resize(count);
            VkResult result = dev->table.GetQueryPoolResults(
                device,
                queryPools[i].handle,
                0,
                count,
                allPoolResults[i].size() * sizeof(uint64_t),
                allPoolResults[i].data(),
                sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
            );
            if (result != VK_SUCCESS) {
                Logger::log("error", "GetQueryPoolResults failed for pool[%zu]: %d", i, result);
                success = false;
                break;
            }
        }

        if (success) {
            float timestampPeriod = dev->props2.properties.limits.timestampPeriod;
            struct AggregatedStat {
                double totalTimeMs = 0.0;
                uint64_t totalSizeBytes = 0;
                uint32_t count = 0;
            };
            std::map<std::string, AggregatedStat> statsRollup;
            std::map<std::string, std::map<VkFormat, AggregatedStat>> formatHist;
            for (const auto& q : trackedQueries) {
                uint64_t startTicks = allPoolResults[q.poolIndex][q.startQueryId];
                uint64_t endTicks = allPoolResults[q.poolIndex][q.endQueryId];
                
                if (endTicks >= startTicks) {
                    double durationMs = (double)(endTicks - startTicks) / (1000000.0f / timestampPeriod);
                    
                    auto& stat = statsRollup[q.label];
                    stat.totalTimeMs += durationMs;
                    stat.totalSizeBytes += q.textureSize;
                    stat.count++;

                    formatHist[q.label][q.format].totalTimeMs += durationMs;
                    formatHist[q.label][q.format].totalSizeBytes += q.textureSize;
                    formatHist[q.label][q.format].count++;
                }
            }
            for (const auto& [label, stat] : statsRollup) {
                double totalSizeMb = static_cast<double>(stat.totalSizeBytes) / (1024.0 * 1024.0);
                auto throughput = [](double sizeMb, double timeMs) -> double {
                    double timeSec = timeMs / 1000.0;
                    return (timeSec > 0.0) ? (sizeMb / timeSec) : 0.0;
                };

                Logger::log("info", "  [%14s] Calls: %-4u | Time: %6.3f ms | Data: %6.1f MB | Throughput: %6.1f MB/s (granularity: %fns)",
                    label.c_str(),
                    stat.count,
                    stat.totalTimeMs,
                    totalSizeMb,
                    throughput(totalSizeMb, stat.totalTimeMs),
                    timestampPeriod);
                for (const auto [key, substat] : formatHist[label]) {
                    double subTotalSizeMb = static_cast<double>(substat.totalSizeBytes) / (1024.0 * 1024.0);
                    Logger::log("info", "     + [%s (%d)], Calls: %-4u | Time: %8.3f ms | Data: %7.1f MB | Throughput: %7.1f MB/s",
                        vk_format_to_string(key).c_str(),
                        key,
                        substat.count,
                        substat.totalTimeMs,
                        subTotalSizeMb,
                        throughput(subTotalSizeMb, substat.totalTimeMs));
                }
            }
            
        }
    }

    for (auto it = stagingBuffers.begin(); it != stagingBuffers.end();) {
        auto buf = std::move(*it);
        it = stagingBuffers.erase(it);
        if (!buf) continue;

        if (!dev->dump_buffers_path.empty()) {
            uint32_t* mappedData;
            VkResult result = dev->table.MapMemory(device, buf->memory, 0, VK_WHOLE_SIZE, 0, (void **) &mappedData);
            if (result != VK_SUCCESS) {
                Logger::log("error", "    MapMemory failed: %d", result);
            }
            VkMappedMemoryRange mapped_memory_range = {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = buf->memory,
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            };
            dev->table.InvalidateMappedMemoryRanges(device, 1, &mapped_memory_range);

            // Logger::log("info", "  Peeking into buffer %p, memory %p, format %d", buf->handle, buf->memory, buf->format);
            // Logger::log("info", "    StagingBuffer %p[0] = 0x%x, 0x%x, 0x%x, 0x%x", buf->handle, mappedData[0], mappedData[1], mappedData[2], mappedData[3]);
            // Logger::log("info", "    StagingBuffer %p[1] = 0x%x, 0x%x, 0x%x, 0x%x", buf->handle, mappedData[4], mappedData[5], mappedData[6], mappedData[7]);
            // Logger::log("info", "    StagingBuffer %p[2] = 0x%x, 0x%x, 0x%x, 0x%x", buf->handle, mappedData[8], mappedData[9], mappedData[10], mappedData[11]);
            // Logger::log("info", "    StagingBuffer %p[3] = 0x%x, 0x%x, 0x%x, 0x%x", buf->handle, mappedData[12], mappedData[13], mappedData[14], mappedData[15]);

            std::stringstream filename;
            std::string id_str = std::to_string(buf->id);
            filename << dev->dump_buffers_path << "/" 
                     << id_str << "_fmt_" << buf->format << "_" << buf->width << "x" << buf->height << ".bin";
            std::ofstream out(filename.str(), std::ios::out | std::ios::binary);
            out.write(reinterpret_cast<const char*>(mappedData), buf->size);
            out.close();

            dev->table.UnmapMemory(device, buf->memory);
        }

        dev->table.DestroyBuffer(device, buf->handle, buf->alloc);
        dev->table.FreeMemory(device, buf->memory, buf->alloc);
    }

    for (auto imageView : stagingImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            dev->table.DestroyImageView(device, imageView, nullptr);
        }
    }
    stagingImageViews.clear();

    for (auto& descriptorSetBlock : descriptorSets) {
        dev->descriptorSetAllocator->free(descriptorSetBlock.first, descriptorSetBlock.second);
    }
    descriptorSets.clear();

    for (auto& poolBlock : queryPools) {
        if (poolBlock.handle != VK_NULL_HANDLE) {
            dev->table.DestroyQueryPool(device, poolBlock.handle, nullptr);
        }
    }
    queryPools.clear();
}

StagingResources::~StagingResources() {
    Cleanup();
}
