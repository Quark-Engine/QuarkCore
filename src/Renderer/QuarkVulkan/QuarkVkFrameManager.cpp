#include "QuarkVkFrameManager.hpp"

#include "../../QuarkInternal.hpp"

#include <stdexcept>

namespace qci
{
    void QuarkVkFrameManager::Initialize(VkDevice device, VkCommandPool commandPool, uint32_t frameCount)
    {
        if (device == VK_NULL_HANDLE || commandPool == VK_NULL_HANDLE || frameCount == 0)
        {
            throw std::runtime_error("Invalid parameters for QuarkVkFrameManager::Initialize.");
        }

        m_Device = device;
        m_CommandPool = commandPool;
        m_vFrames.resize(frameCount);

        std::vector<VkCommandBuffer> vCommandBuffers(frameCount);
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = frameCount;

        if (vkAllocateCommandBuffers(device, &allocInfo, vCommandBuffers.data()) != VK_SUCCESS)
        {
            m_vFrames.clear();
            m_CommandPool = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to allocate Vulkan command buffers.");
        }

        for (uint32_t i = 0; i < frameCount; ++i)
        {
            VkFrameDataExt& frame = m_vFrames[i];
            frame.m_CommandBuffer = vCommandBuffers[i];

            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.m_ImageAvailable) != VK_SUCCESS ||
                vkCreateFence(device, &fenceInfo, nullptr, &frame.m_InFlightFence) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create Vulkan synchronization objects.");
            }
        }

        TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Allocated %u frame command buffers and synchronization objects.", frameCount));
    }

    void QuarkVkFrameManager::SetSwapchainImageCount(uint32_t imageCount)
    {
        for (VkSemaphore semaphore : m_vRenderFinishedByImage)
        {
            if (semaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(m_Device, semaphore, nullptr);
            }
        }
        m_vRenderFinishedByImage.clear();
        m_vRenderFinishedByImage.resize(imageCount, VK_NULL_HANDLE);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (VkSemaphore& semaphore : m_vRenderFinishedByImage)
        {
            if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create Vulkan presentation semaphore.");
            }
        }
    }

    void QuarkVkFrameManager::Shutdown()
    {
        for (VkFrameDataExt& frame : m_vFrames)
        {
            if (frame.m_CommandBuffer != VK_NULL_HANDLE)
            {
                vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &frame.m_CommandBuffer);
                frame.m_CommandBuffer = VK_NULL_HANDLE;
            }
            if (frame.m_ImageAvailable != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(m_Device, frame.m_ImageAvailable, nullptr);
                frame.m_ImageAvailable = VK_NULL_HANDLE;
            }
            if (frame.m_InFlightFence != VK_NULL_HANDLE)
            {
                vkDestroyFence(m_Device, frame.m_InFlightFence, nullptr);
                frame.m_InFlightFence = VK_NULL_HANDLE;
            }
        }
        for (VkSemaphore semaphore : m_vRenderFinishedByImage)
        {
            if (semaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(m_Device, semaphore, nullptr);
            }
        }
        m_vRenderFinishedByImage.clear();
        m_vFrames.clear();
        m_CommandPool = VK_NULL_HANDLE;
        m_Device = VK_NULL_HANDLE;
    }

    void QuarkVkFrameManager::BeginFrame(uint32_t frameIndex) const
    {
        const VkFrameDataExt& frame = m_vFrames[frameIndex];
        vkWaitForFences(m_Device, 1, &frame.m_InFlightFence, VK_TRUE, UINT64_MAX);
    }

    VkResult QuarkVkFrameManager::AcquireNextImage(VkSwapchainKHR swapchain, uint32_t frameIndex,
                                                   uint32_t& outImageIndex, uint64_t timeout) const
    {
        const VkFrameDataExt& frame = m_vFrames[frameIndex];
        return vkAcquireNextImageKHR(m_Device, swapchain, timeout,
                                     frame.m_ImageAvailable, VK_NULL_HANDLE, &outImageIndex);
    }

    void QuarkVkFrameManager::ResetFrame(uint32_t frameIndex)
    {
        VkFrameDataExt& frame = m_vFrames[frameIndex];
        vkResetFences(m_Device, 1, &frame.m_InFlightFence);
        if (frame.m_CommandBuffer != VK_NULL_HANDLE)
        {
            vkResetCommandBuffer(frame.m_CommandBuffer, 0);
        }
    }

    bool QuarkVkFrameManager::Submit(uint32_t frameIndex, uint32_t imageIndex, VkQueue graphicsQueue)
    {
        if (imageIndex >= m_vRenderFinishedByImage.size())
        {
            return false;
        }
        const VkFrameDataExt& frame = m_vFrames[frameIndex];
        const VkSemaphore renderFinished = m_vRenderFinishedByImage[imageIndex];

        VkSemaphore aWaitSemaphores[] =
            {
                frame.m_ImageAvailable};
        VkPipelineStageFlags aWaitStages[] =
            {
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore aSignalSemaphores[] =
            {
                renderFinished};

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = aWaitSemaphores;
        submitInfo.pWaitDstStageMask = aWaitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &frame.m_CommandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = aSignalSemaphores;

        return vkQueueSubmit(graphicsQueue, 1, &submitInfo, frame.m_InFlightFence) == VK_SUCCESS;
    }

    VkResult QuarkVkFrameManager::Present(VkSwapchainKHR swapchain, uint32_t frameIndex,
                                          uint32_t imageIndex, VkQueue presentQueue)
    {
        (void)frameIndex;
        if (imageIndex >= m_vRenderFinishedByImage.size())
        {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        const VkSemaphore renderFinished = m_vRenderFinishedByImage[imageIndex];

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;

        return vkQueuePresentKHR(presentQueue, &presentInfo);
    }
} // qci
