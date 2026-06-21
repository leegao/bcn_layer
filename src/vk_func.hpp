#ifndef __VK_FUNC_HPP
#define __VK_FUNC_HPP

#include <vulkan/vulkan.h>

extern "C" {

VkResult VKAPI_CALL
BCnLayer_CreateImage(VkDevice device,
                     const VkImageCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkImage *pImage);

VkResult VKAPI_CALL
BCnLayer_CreateImageView(VkDevice device,
                         const VkImageViewCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkImageView *pImageView);

void VKAPI_CALL
BCnLayer_DestroyImage(VkDevice device,
					  VkImage image,
                      const VkAllocationCallbacks *pAllocator);

VkResult VKAPI_CALL
BCnLayer_CreateBuffer(VkDevice device,
                      const VkBufferCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkBuffer *pBuffer);
                      
VkResult VKAPI_CALL
BCnLayer_BindBufferMemory(VkDevice device,
                          VkBuffer buffer,
                          VkDeviceMemory memory,
                          VkDeviceSize memoryOffset);

VkResult VKAPI_CALL
BCnLayer_BindBufferMemory2(VkDevice device,
                           uint32_t bindInfoCount,
                           const VkBindBufferMemoryInfo* pBindInfos);

void VKAPI_CALL
BCnLayer_DestroyBuffer(VkDevice device,
					   VkBuffer buffer,
                       const VkAllocationCallbacks *pAllocator);

VkResult VKAPI_CALL
BCnLayer_AllocateCommandBuffers(VkDevice device,
                                const VkCommandBufferAllocateInfo *pAllocateInfo,
                                VkCommandBuffer *pCommandBuffers);

void VKAPI_CALL
BCnLayer_FreeCommandBuffers(VkDevice device,
                            VkCommandPool commandPool,
                            uint32_t commandBufferCount,
                            const VkCommandBuffer *pCommandBuffers);

void VKAPI_CALL
BCnLayer_CmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                              VkBuffer srcBuffer,
                              VkImage dstImage,
                              VkImageLayout dstImageLayout,
                              uint32_t regionCount,
                              const VkBufferImageCopy *pRegions);


void VKAPI_CALL
BCnLayer_CmdCopyBufferToImage2(VkCommandBuffer commandBuffer, 
                               const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo);

void VKAPI_CALL
BCnLayer_CmdCopyImage2(VkCommandBuffer commandBuffer,
                       const VkCopyImageInfo2* pCopyImageInfo);

void VKAPI_CALL
BCnLayer_CmdCopyImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkImageCopy* pRegions);

void VKAPI_CALL
BCnLayer_GetDeviceQueue(VkDevice device,
                        uint32_t queueFamilyIndex,
                        uint32_t queueIndex,
                        VkQueue *pQueue);

VkResult VKAPI_CALL
BCnLayer_QueueSubmit(VkQueue queue,
                     uint32_t submitInfoCount,
                     const VkSubmitInfo *pSubmitInfos,
                     VkFence fence);

VkResult VKAPI_CALL
BCnLayer_QueueSubmit2(VkQueue queue,
                     uint32_t submitInfoCount,
                     const VkSubmitInfo2 *pSubmitInfos,
                     VkFence fence);

VkResult VKAPI_CALL
BCnLayer_CreateFence(VkDevice device,
                     const VkFenceCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkFence *pFence);
                     
VkResult VKAPI_CALL
BCnLayer_WaitForFences(VkDevice device,
                       uint32_t fenceCount,
                       const VkFence *pFences,
                       VkBool32 waitAll,
                       uint64_t timeout);

void VKAPI_CALL
BCnLayer_DestroyFence(VkDevice device,
                      VkFence fence,
                      const VkAllocationCallbacks *pAllocator);              
}



#endif
