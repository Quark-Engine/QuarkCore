#include "QuarkVkFrameManager.hpp"

#include "../../QuarkInternal.hpp"

#include <stdexcept>
namespace qci {
void QuarkVkFrameManager::Initialize(VkDevice device, VkCommandPool commandPool, uint32_t frameCount) {
    if (device == VK_NULL_HANDLE || commandPool == VK_NULL_HANDLE || frameCount == 0) {
        throw std::runtime_error("Invalid parameters for QuarkVkFrameManager::Initialize.");
    }

    m_device      = device;
    m_commandPool = commandPool;
    m_frames.resize(frameCount);

    std::vector<VkCommandBuffer> commandBuffers(frameCount);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = frameCount;

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        m_frames.clear();
        m_commandPool = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to allocate Vulkan command buffers.");
    }

    for (uint32_t i = 0; i < frameCount; ++i) {
        VkFrameDataExt& frame = m_frames[i];
        frame.commandBuffer   = commandBuffers[i];

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlightFence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan synchronization objects.");
        }
    }

    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Allocated %u frame command buffers and synchronization objects.", frameCount));
}

void QuarkVkFrameManager::SetSwapchainImageCount(uint32_t imageCount) {
    for (VkSemaphore semaphore : m_renderFinishedByImage) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, semaphore, nullptr);
        }
    }
    m_renderFinishedByImage.clear();
    m_renderFinishedByImage.resize(imageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkSemaphore& semaphore : m_renderFinishedByImage) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan presentation semaphore.");
        }
    }
}

void QuarkVkFrameManager::Shutdown() {
    for (VkFrameDataExt& frame : m_frames) {
        if (frame.commandBuffer  != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(m_device, m_commandPool, 1, &frame.commandBuffer);
            frame.commandBuffer = VK_NULL_HANDLE;
        }
        if (frame.imageAvailable != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, frame.imageAvailable, nullptr);
            frame.imageAvailable = VK_NULL_HANDLE;
        }
        if (frame.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, frame.inFlightFence, nullptr);
            frame.inFlightFence = VK_NULL_HANDLE;
        }
    }
    for (VkSemaphore semaphore : m_renderFinishedByImage) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, semaphore, nullptr);
        }
    }
    m_renderFinishedByImage.clear();
    m_frames.clear();
    m_commandPool = VK_NULL_HANDLE;
    m_device      = VK_NULL_HANDLE;
}

void QuarkVkFrameManager::BeginFrame(uint32_t frameIndex) const {
    const VkFrameDataExt& frame = m_frames[frameIndex];
    vkWaitForFences(m_device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);
}

VkResult QuarkVkFrameManager::AcquireNextImage(VkSwapchainKHR swapchain, uint32_t frameIndex,
                                               uint32_t& outImageIndex, uint64_t timeout) const {
    const VkFrameDataExt& frame = m_frames[frameIndex];
    return vkAcquireNextImageKHR(m_device, swapchain, timeout,
                                 frame.imageAvailable, VK_NULL_HANDLE, &outImageIndex);
}

void QuarkVkFrameManager::ResetFrame(uint32_t frameIndex) {
    VkFrameDataExt& frame = m_frames[frameIndex];
    vkResetFences(m_device, 1, &frame.inFlightFence);
    if (frame.commandBuffer != VK_NULL_HANDLE) {
        vkResetCommandBuffer(frame.commandBuffer, 0);
    }
}

bool QuarkVkFrameManager::Submit(uint32_t frameIndex, uint32_t imageIndex, VkQueue graphicsQueue) {
    if (imageIndex >= m_renderFinishedByImage.size()) return false;
    const VkFrameDataExt& frame = m_frames[frameIndex];
    const VkSemaphore renderFinished = m_renderFinishedByImage[imageIndex];

    VkSemaphore waitSemaphores[]   = { frame.imageAvailable };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { renderFinished };

    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = waitSemaphores;
    submitInfo.pWaitDstStageMask    = waitStages;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = signalSemaphores;

    return vkQueueSubmit(graphicsQueue, 1, &submitInfo, frame.inFlightFence) == VK_SUCCESS;
}

VkResult QuarkVkFrameManager::Present(VkSwapchainKHR swapchain, uint32_t frameIndex,
                                      uint32_t imageIndex, VkQueue presentQueue) {
    (void)frameIndex;
    if (imageIndex >= m_renderFinishedByImage.size()) return VK_ERROR_INITIALIZATION_FAILED;
    const VkSemaphore renderFinished = m_renderFinishedByImage[imageIndex];

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderFinished;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &swapchain;
    presentInfo.pImageIndices      = &imageIndex;

    return vkQueuePresentKHR(presentQueue, &presentInfo);
}
} // namespace qci
