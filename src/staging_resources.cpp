#include "staging_resources.hpp"

#include "astc_debug.h"
#include "bcn.hpp"
#include "bcn_layer.hpp"
#include "buffer.hpp"
#include "command_buffer.hpp"
#include "logger.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <map>

void LogDeviceFault(struct device *dev, const char* call) {
    Logger::log("error", "FATAL: %s failed with a GPU fault, the game will now crash", call);

    if (!dev->table.GetDeviceFaultInfoEXT) {
        Logger::log("error", "+ vkGetDeviceFaultInfoEXT is not available, cannot dump vendor specific fault info");
        return;
    }

    VkDeviceFaultCountsEXT fault_counts = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT,
    };
    VkResult result = dev->table.GetDeviceFaultInfoEXT(dev->handle, &fault_counts, NULL);
    if (result != VK_SUCCESS) {
        return;
    }

    if (fault_counts.addressInfoCount == 0 &&
        fault_counts.vendorInfoCount == 0 &&
        fault_counts.vendorBinarySize == 0) {
        Logger::log("error", "+ Device lost, but no fault info was recorded by the driver.");
        return;
    }

    VkDeviceFaultInfoEXT fault_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,
    };

    std::vector<VkDeviceFaultAddressInfoEXT> addressInfos(fault_counts.addressInfoCount);
    std::vector<VkDeviceFaultVendorInfoEXT> vendorInfos(fault_counts.vendorInfoCount);
    std::vector<char> vendorBinaryData(fault_counts.vendorBinarySize);

    fault_info.pAddressInfos = addressInfos.data();
    fault_info.pVendorInfos = vendorInfos.data();
    fault_info.pVendorBinaryData = vendorBinaryData.data();

    result = dev->table.GetDeviceFaultInfoEXT(dev->handle, &fault_counts, &fault_info);
    if (result == VK_SUCCESS) {
        Logger::log("error", "--- VULKAN DEVICE FAULT DETECTED ---");
        Logger::log("error", "Description: %s", fault_info.description);

        for (uint32_t i = 0; i < fault_counts.addressInfoCount; i++) {
            Logger::log("error", ".pAddressInfos[%d]", i);
            Logger::log("error", "  addressType: %d", fault_info.pAddressInfos[i].addressType);
            Logger::log("error", "  reportedAddress: %llu", fault_info.pAddressInfos[i].reportedAddress);
            Logger::log("error", "  addressPrecision: %llu", fault_info.pAddressInfos[i].addressPrecision);
        }
        for (uint32_t i = 0; i < fault_counts.vendorInfoCount; i++) {
            Logger::log("error", ".pVendorInfos[%d]", i);
            Logger::log("error", "  description: %s", fault_info.pVendorInfos[i].description);
            Logger::log("error", "  vendorFaultCode: %llu", fault_info.pVendorInfos[i].vendorFaultCode);
            Logger::log("error", "  vendorFaultData: %llu", fault_info.pVendorInfos[i].vendorFaultData);
        }
        if (fault_info.pVendorBinaryData && fault_counts.vendorBinarySize > 0) {
            Logger::log("error", "Vendor binary crash dump retrieved (%llu bytes).", fault_counts.vendorBinarySize);
            std::string line_buffer;
            line_buffer.reserve(256);
            for (uint32_t i = 0; i < fault_counts.vendorBinarySize; i++) {
                char byte_str[4];
                snprintf(byte_str, sizeof(byte_str), "%02x ", vendorBinaryData[i]);
                line_buffer += byte_str;
                if ((i + 1) % 64 == 0 || (i + 1) == fault_counts.vendorBinarySize) {
                    Logger::log("error", "  %s", line_buffer.c_str());
                    line_buffer.clear();
                }
            }
        }
        Logger::log("error", "--- END FAULT INFO ---");
    }
}

void ScopedTimestampQuery::Start() {
   	if (m_startQueryId != UINT32_MAX) {
        m_cb->device->table.CmdWriteTimestamp(m_cb->handle, m_startStage, m_queryPool, m_startQueryId);
    }
}

void ScopedTimestampQuery::End() {
    if (m_startQueryId != UINT32_MAX) {
        m_cb->device->table.CmdWriteTimestamp(m_cb->handle, m_endStage, m_queryPool, m_startQueryId + 1);
    }
    m_startQueryId = UINT32_MAX;
}

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
    VkResult result = dev->table.WaitForFences(device, 1, &completed, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        Logger::log("error", "WaitForFences failed with result %d", result);
        if (result == VK_ERROR_DEVICE_LOST){
            LogDeviceFault(dev, "WaitForFences");
        }
    }
    has_completed = true;
}

ScopedTimestampQuery StagingResources::MakeScopedTimestampQuery(
    struct command_buffer* cb,
    const std::string& label,
    VkFormat format,
    uint64_t texture_size,
    VkPipelineStageFlagBits startStage,
    VkPipelineStageFlagBits endStage) {
    auto *dev = get_device(device);
    if (!dev || !dev->profile_transfers)
        return ScopedTimestampQuery { cb, label, format, texture_size, VK_NULL_HANDLE, UINT32_MAX, startStage, endStage };

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
            return ScopedTimestampQuery { cb, label, format, texture_size, VK_NULL_HANDLE, UINT32_MAX, startStage, endStage };
        }
        dev->table.CmdResetQueryPool(cb->handle, newPool, 0, kPoolBlockSize);
        queryPools.push_back({ newPool, 0 });
    }

    auto& activeBlock = queryPools.back();
    uint32_t startId = activeBlock.allocatedQueries;
    activeBlock.allocatedQueries += 2;

    auto pool = activeBlock.handle;
    size_t activePoolIdx = queryPools.size() - 1;

    trackedQueries.push_back({ label, format, texture_size, activePoolIdx, startId, startId + 1 });
    return ScopedTimestampQuery { cb, label, format, texture_size, pool, startId, startStage, endStage };
}

void StagingResources::Cleanup() {
    if (freed) return;
    if (IsEmpty()) return;
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
        Logger::log("info", "Cleaning up batch %d with %d buffers, %d descriptors, and %d tracked queries took %d ms",
            id, stagingBuffers.size(), descriptorSets.size(), trackedQueries.size(), timestamp - this->timestamp);
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
            static std::map<std::string, AggregatedStat> globalStatsRollup;
            std::map<std::string, std::map<VkFormat, AggregatedStat>> formatHist;
            static std::map<std::string, std::map<VkFormat, AggregatedStat>> globalFormatHist;
            for (const auto& q : trackedQueries) {
                uint64_t startTicks = allPoolResults[q.poolIndex][q.startQueryId];
                uint64_t endTicks = allPoolResults[q.poolIndex][q.endQueryId];

                if (endTicks >= startTicks) {
                    double durationMs = (double)(endTicks - startTicks) / (1000000.0f / timestampPeriod);

                    auto& stat = statsRollup[q.label];
                    stat.totalTimeMs += durationMs;
                    stat.totalSizeBytes += q.textureSize;
                    stat.count++;

                    auto& globalStat = globalStatsRollup[q.label];
                    globalStat.totalTimeMs += durationMs;
                    globalStat.totalSizeBytes += q.textureSize;
                    globalStat.count++;

                    if (dev->profile_more_transfers) {
                        formatHist[q.label][q.format].totalTimeMs += durationMs;
                        formatHist[q.label][q.format].totalSizeBytes += q.textureSize;
                        formatHist[q.label][q.format].count++;
                        globalFormatHist[q.label][q.format].totalTimeMs += durationMs;
                        globalFormatHist[q.label][q.format].totalSizeBytes += q.textureSize;
                        globalFormatHist[q.label][q.format].count++;
                    }
                }
            }
            for (const auto& [label, stat] : statsRollup) {
                auto& globalStat = globalStatsRollup[label];
                double totalSizeMb = static_cast<double>(stat.totalSizeBytes) / (1024.0 * 1024.0);
                double globalTotalSizeMb = static_cast<double>(globalStat.totalSizeBytes) / (1024.0 * 1024.0);
                auto throughput = [](double sizeMb, double timeMs) -> double {
                    double timeSec = timeMs / 1000.0;
                    return (timeSec > 0.0) ? (sizeMb / timeSec) : 0.0;
                };

                Logger::log("info", "  [%14s] Calls: %-5u | Time: %6.2f ms | Data: %6.1f MB | Throughput: %6.1f MB/s (granularity: %.1fns)",
                    label.c_str(),
                    stat.count,
                    stat.totalTimeMs,
                    totalSizeMb,
                    throughput(totalSizeMb, stat.totalTimeMs),
                    timestampPeriod);

                if (dev->profile_more_transfers) {
                    Logger::log("info", "  %22s: %-5u |  %11.2f ms |  %11.1f MB | Throughput: %6.1f MB/s",
                        "+ total calls",
                        globalStat.count,
                        globalStat.totalTimeMs,
                        globalTotalSizeMb,
                        throughput(globalTotalSizeMb, globalStat.totalTimeMs),
                        timestampPeriod);

                    for (const auto [key, substat] : globalFormatHist[label]) {
                        double subTotalSizeMb = static_cast<double>(substat.totalSizeBytes) / (1024.0 * 1024.0);
                        Logger::log("info", "            + %4s (%d): %-5u |  %11.2f ms |  %11.1f MB | Throughput: %6.1f MB/s",
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
    }

    uint32_t count = 0;
    uint64_t total_error_squared = 0;
    uint64_t total_quantized_error_squared = 0;
    uint64_t total_quantized_alt_error_squared = 0;

    static uint32_t global_count = 0;
    static uint64_t global_total_error_squared = 0;
    static uint64_t global_total_quantized_error_squared = 0;
    static uint64_t global_total_quantized_alt_error_squared = 0;

    uint64_t total_color_spread_squared = 0;
    uint64_t total_weights_squared = 0;
    uint64_t total_weights = 0;
    uint64_t total_quantized_weight_errors_3 = 0;
    uint64_t total_quantized_weight_errors_7 = 0;
    uint64_t total_quantized_weight_errors_15 = 0;
    uint64_t total_quantized_color_errors_47 = 0;
    uint64_t total_quantized_color_errors_191 = 0;

    uint64_t total_quantized_pixel_error_191_15_exact = 0;
    uint64_t total_quantized_pixel_error_47_15_exact = 0;
    uint64_t total_quantized_pixel_error_255_7_exact = 0;
    uint64_t total_quantized_pixel_error_255_3_exact = 0;

    uint64_t total_quantized_pixel_error_191_15_approx = 0;
    uint64_t total_quantized_pixel_error_47_15_approx = 0;
    uint64_t total_quantized_pixel_error_255_7_approx = 0;
    uint64_t total_quantized_pixel_error_255_3_approx = 0;

    uint64_t total_quantized_weight_errors_refined_3 = 0;
    uint64_t total_quantized_weight_errors_refined_7 = 0;
    uint64_t total_quantized_weight_errors_refined_15 = 0;

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

        if (dev->debug_astc && buf->format == VK_FORMAT_UNDEFINED && buf->label == "analysis_output") {
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

            AstcAnalysis* debugData = reinterpret_cast<AstcAnalysis*>(mappedData);
            // Logger::log("info", "     + %d x %d, count: %d", buf->width, buf->height, debugData->count);
            count += debugData->count;
            total_error_squared += debugData->sum_error_squared;
            total_quantized_error_squared += debugData->sum_quantization_error_squared;
            total_quantized_alt_error_squared += debugData->sum_quantization_alt_error_squared;

            global_count += debugData->count;
            global_total_error_squared += debugData->sum_error_squared;
            global_total_quantized_error_squared += debugData->sum_quantization_error_squared;
            global_total_quantized_alt_error_squared += debugData->sum_quantization_alt_error_squared;

            total_color_spread_squared += debugData->sum_color_spread_squared;
            total_weights_squared += debugData->sum_weights_squared;
            total_weights += debugData->sum_weights;
            total_quantized_weight_errors_3 += debugData->quantized_weight_errors_3;
            total_quantized_weight_errors_7 += debugData->quantized_weight_errors_7;
            total_quantized_weight_errors_15 += debugData->quantized_weight_errors_15;
            total_quantized_color_errors_47 += debugData->quantized_color_errors_47;
            total_quantized_color_errors_191 += debugData->quantized_color_errors_191;

            total_quantized_weight_errors_refined_3 += debugData->quantized_weight_errors_refined_3;
            total_quantized_weight_errors_refined_7 += debugData->quantized_weight_errors_refined_7;
            total_quantized_weight_errors_refined_15 += debugData->quantized_weight_errors_refined_15;

            total_quantized_pixel_error_191_15_exact += debugData->quantized_pixel_error_191_15_exact;
            total_quantized_pixel_error_47_15_exact += debugData->quantized_pixel_error_47_15_exact;
            total_quantized_pixel_error_255_7_exact += debugData->quantized_pixel_error_255_7_exact;
            total_quantized_pixel_error_255_3_exact += debugData->quantized_pixel_error_255_3_exact;

            total_quantized_pixel_error_191_15_approx += debugData->quantized_pixel_error_191_15_approx;
            total_quantized_pixel_error_47_15_approx += debugData->quantized_pixel_error_47_15_approx;
            total_quantized_pixel_error_255_7_approx += debugData->quantized_pixel_error_255_7_approx;
            total_quantized_pixel_error_255_3_approx += debugData->quantized_pixel_error_255_3_approx;

            dev->table.UnmapMemory(device, buf->memory);
        }

        dev->table.DestroyBuffer(device, buf->handle, buf->alloc);
        dev->table.FreeMemory(device, buf->memory, buf->alloc);
    }

    if (dev->debug_astc) {
        Logger::log("info", "  Diagnostics (%d blocks, %d total)", count, global_count);
        auto mse = (double) total_error_squared / 48 / 65025 / count;
        auto global_mse = (double) global_total_error_squared / 48 / 65025 / global_count;
        Logger::log("info", "     + mean_error_squared: %lf (PSNR: %.2lf), running: %lf (PSNR: %.2lf)", mse, -10.0 * std::log10(mse), global_mse, -10.0 * std::log10(global_mse));
        auto quantized_mse = (double) total_quantized_error_squared / 48 / 65025 / count;
        auto global_quantized_mse = (double) global_total_quantized_error_squared / 48 / 65025 / global_count;
        Logger::log("info", "     + mean_quantized_error_squared: %lf (PSNR: %.2lf), running: %lf (PSNR: %.2lf)", quantized_mse, -10.0 * std::log10(quantized_mse), global_quantized_mse, -10.0 * std::log10(global_quantized_mse));
        auto quantized_alt_mse = (double) total_quantized_alt_error_squared / 48 / 65025 / count;
        auto global_quantized_alt_mse = (double) global_total_quantized_alt_error_squared / 48 / 65025 / global_count;
        Logger::log("info", "     + mean_quantized_error_squared (alt): %lf (PSNR: %.2lf), running: %lf (PSNR: %.2lf)", quantized_alt_mse, -10.0 * std::log10(quantized_alt_mse), global_quantized_alt_mse, -10.0 * std::log10(global_quantized_alt_mse));

        if (dev->more_debug_astc) {
            Logger::log("info", "     + mean_color_spread_squared: %lf", (double) total_color_spread_squared / 3 / 65025 / count);
            Logger::log("info", "     + mean_weights_squared: %lf", (double) total_weights_squared / 16 / 65025 / count);
            Logger::log("info", "     + mean_weights: %lf", (double) total_weights / 16 / 255 / count);

            Logger::log("info", "     + mean_quantized_weight_errors__3: %.15f vs expected: %.15f or %.15f",
                (double) total_quantized_weight_errors_3 / 16 / 65025 / count,
                (double) total_quantized_weight_errors_refined_3 / 16 / 65025 / count,
                1/(12.0 * 3 * 3));
            Logger::log("info", "     + mean_quantized_weight_errors__7: %.15f vs expected: %.15f or %.15f",
                (double) total_quantized_weight_errors_7 / 16 / 65025 / count,
                (double) total_quantized_weight_errors_refined_7 / 16 / 65025 / count,
                1/(12.0 * 7 * 7));
            Logger::log("info", "     + mean_quantized_weight_errors_15: %.15f vs expected: %.15f or %.15f",
                (double) total_quantized_weight_errors_15 / 16 / 65025 / count,
                (double) total_quantized_weight_errors_refined_15 / 16 / 65025 / count,
                1/(12.0 * 15 * 15));

            Logger::log("info", "     + mean_quantized_color_errors__47: %.15f vs expected: %.15f", (double) total_quantized_color_errors_47 / 6 / 65025 / count, 1/(12.0 * 47 * 47));
            Logger::log("info", "     + mean_quantized_color_errors_191: %.15f vs expected: %.15f", (double) total_quantized_color_errors_191 / 6 / 65025 / count, 1/(12.0 * 191 * 191));

            Logger::log("info", "     + mean_quantized_pixel_error__47_15: %.15f vs actual: %.15f", (double) total_quantized_pixel_error_47_15_approx / 48 / 65025 / count, (double) total_quantized_pixel_error_47_15_exact / 48 / 65025 / count);
            Logger::log("info", "     + mean_quantized_pixel_error_191_15: %.15f vs actual: %.15f", (double) total_quantized_pixel_error_191_15_approx / 48 / 65025 / count, (double) total_quantized_pixel_error_191_15_exact / 48 / 65025 / count);
            Logger::log("info", "     + mean_quantized_pixel_error_255__7: %.15f vs actual: %.15f", (double) total_quantized_pixel_error_255_7_approx / 48 / 65025 / count, (double) total_quantized_pixel_error_255_7_exact / 48 / 65025 / count);
            Logger::log("info", "     + mean_quantized_pixel_error_255__3: %.15f vs actual: %.15f", (double) total_quantized_pixel_error_255_3_approx / 48 / 65025 / count, (double) total_quantized_pixel_error_255_3_exact / 48 / 65025 / count);
        }
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
