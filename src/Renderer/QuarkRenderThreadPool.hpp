#ifndef __QUARK_RENDER_THREAD_POOL__
#define __QUARK_RENDER_THREAD_POOL__

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace qci {

class QuarkRenderThreadPool {
public:
    QuarkRenderThreadPool() = default;
    ~QuarkRenderThreadPool() {
        Shutdown();
    }

    QuarkRenderThreadPool(const QuarkRenderThreadPool&) = delete;
    QuarkRenderThreadPool& operator=(const QuarkRenderThreadPool&) = delete;

    void Initialize(uint32_t requestedWorkers = 0) {
        Shutdown();

        const uint32_t hwThreads = std::max(1u, std::thread::hardware_concurrency());
        m_workerCount = requestedWorkers > 0
            ? std::clamp(requestedWorkers, 1u, 32u)
            : std::clamp(hwThreads, 2u, 16u);

        m_stopping = false;
        m_generation = 0;
        m_pendingWorkers = 0;

        if (m_workerCount <= 1) {
            return;
        }

        const uint32_t backgroundCount = m_workerCount - 1;
        m_threads.reserve(backgroundCount);
        for (uint32_t i = 0; i < backgroundCount; ++i) {
            const uint32_t workerIndex = i + 1;
            m_threads.emplace_back([this, workerIndex]() {
                WorkerLoop(workerIndex);
            });
        }
    }

    void Shutdown() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_cvWork.notify_all();

        for (std::thread& thread : m_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        m_threads.clear();
        m_workerCount = 1;
        m_pendingWorkers = 0;
        m_job = nullptr;
    }

    uint32_t WorkerCount() const {
        return std::max(1u, m_workerCount);
    }

    bool IsMultithreaded() const {
        return m_workerCount > 1 && !m_threads.empty();
    }

    uint64_t CompletedBatches() const {
        return m_completedBatches.load(std::memory_order_relaxed);
    }

    template <typename Fn>
    void ParallelFor(size_t itemCount, size_t minBatchSize, Fn&& fn) {
        if (itemCount == 0) {
            return;
        }

        const size_t batchSize = std::max<size_t>(1, minBatchSize);
        const size_t batchCount = (itemCount + batchSize - 1) / batchSize;

        if (batchCount <= 1 || !IsMultithreaded()) {
            fn(0u, 0u, itemCount);
            m_completedBatches.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        std::exception_ptr capturedException = nullptr;
        std::mutex exceptionMutex;

        auto executeBatches = [&](uint32_t workerIndex) {
            while (true) {
                const size_t batchIdx = m_nextBatch.fetch_add(1, std::memory_order_relaxed);
                if (batchIdx >= batchCount) {
                    break;
                }
                const size_t begin = batchIdx * batchSize;
                const size_t end = std::min(itemCount, begin + batchSize);
                try {
                    fn(workerIndex, begin, end);
                    m_completedBatches.fetch_add(1, std::memory_order_relaxed);
                } catch (...) {
                    std::lock_guard<std::mutex> exLock(exceptionMutex);
                    if (!capturedException) {
                        capturedException = std::current_exception();
                    }
                }
            }
        };

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_nextBatch.store(0, std::memory_order_relaxed);
            m_job = executeBatches;
            m_pendingWorkers = static_cast<uint32_t>(m_threads.size());
            ++m_generation;
        }
        m_cvWork.notify_all();

        executeBatches(0u);

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cvDone.wait(lock, [this]() {
                return m_pendingWorkers == 0;
            });
            m_job = nullptr;
        }

        if (capturedException) {
            std::rethrow_exception(capturedException);
        }
    }

private:
    void WorkerLoop(uint32_t workerIndex) {
        uint64_t seenGeneration = 0;
        while (true) {
            std::function<void(uint32_t)> localJob;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cvWork.wait(lock, [this, &seenGeneration]() {
                    return m_stopping || m_generation != seenGeneration;
                });
                if (m_stopping) {
                    return;
                }
                seenGeneration = m_generation;
                localJob = m_job;
            }

            if (localJob) {
                localJob(workerIndex);
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_pendingWorkers > 0) {
                    --m_pendingWorkers;
                    if (m_pendingWorkers == 0) {
                        m_cvDone.notify_one();
                    }
                }
            }
        }
    }

    uint32_t m_workerCount = 1;
    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_cvWork;
    std::condition_variable m_cvDone;
    uint64_t m_generation = 0;
    uint32_t m_pendingWorkers = 0;
    bool m_stopping = false;
    std::atomic<size_t> m_nextBatch{0};
    std::atomic<uint64_t> m_completedBatches{0};
    std::function<void(uint32_t)> m_job;
};

} // namespace qci

#endif // __QUARK_RENDER_THREAD_POOL__
