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

namespace qci
{
    class QuarkRenderThreadPool
    {
    public:
        QuarkRenderThreadPool() = default;

        ~QuarkRenderThreadPool()
        {
            Shutdown();
        }

        QuarkRenderThreadPool(const QuarkRenderThreadPool&) = delete;
        QuarkRenderThreadPool& operator=(const QuarkRenderThreadPool&) = delete;

        void Initialize(uint32_t requestedWorkers = 0)
        {
            Shutdown();

            const uint32_t hwThreads = std::max(1u, std::thread::hardware_concurrency());
            m_WorkerCount = requestedWorkers > 0
                                ? std::clamp(requestedWorkers, 1u, 32u)
                                : std::clamp(hwThreads, 2u, 16u);

            m_Stopping = false;
            m_Generation = 0;
            m_PendingWorkers = 0;

            if (m_WorkerCount <= 1)
            {
                return;
            }

            const uint32_t backgroundCount = m_WorkerCount - 1;
            m_vThreads.reserve(backgroundCount);
            for (uint32_t i = 0; i < backgroundCount; ++i)
            {
                const uint32_t workerIndex = i + 1;
                m_vThreads.emplace_back([this, workerIndex]()
                                        {
                                            WorkerLoop(workerIndex);
                                        });
            }
        }

        void Shutdown()
        {
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_Stopping = true;
            }
            m_CvWork.notify_all();

            for (std::thread& thread : m_vThreads)
            {
                if (thread.joinable())
                {
                    thread.join();
                }
            }
            m_vThreads.clear();
            m_WorkerCount = 1;
            m_PendingWorkers = 0;
            m_Job = nullptr;
        }

        uint32_t WorkerCount() const
        {
            return std::max(1u, m_WorkerCount);
        }

        bool IsMultithreaded() const
        {
            return m_WorkerCount > 1 && !m_vThreads.empty();
        }

        uint64_t CompletedBatches() const
        {
            return m_CompletedBatches.load(std::memory_order_relaxed);
        }

        template <typename Fn>
        void ParallelFor(size_t itemCount, size_t minBatchSize, Fn&& fn)
        {
            if (itemCount == 0)
            {
                return;
            }

            const size_t batchSize = std::max<size_t>(1, minBatchSize);
            const size_t batchCount = (itemCount + batchSize - 1) / batchSize;

            if (batchCount <= 1 || !IsMultithreaded())
            {
                fn(0u, 0u, itemCount);
                m_CompletedBatches.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            std::exception_ptr capturedException = nullptr;
            std::mutex exceptionMutex;

            auto executeBatches = [&](uint32_t workerIndex)
            {
                while (true)
                {
                    const size_t batchIdx = m_NextBatch.fetch_add(1, std::memory_order_relaxed);
                    if (batchIdx >= batchCount)
                    {
                        break;
                    }
                    const size_t begin = batchIdx * batchSize;
                    const size_t end = std::min(itemCount, begin + batchSize);
                    try
                    {
                        fn(workerIndex, begin, end);
                        m_CompletedBatches.fetch_add(1, std::memory_order_relaxed);
                    }
                    catch (...)
                    {
                        std::lock_guard<std::mutex> exLock(exceptionMutex);
                        if (!capturedException)
                        {
                            capturedException = std::current_exception();
                        }
                    }
                }
            };

            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_NextBatch.store(0, std::memory_order_relaxed);
                m_Job = executeBatches;
                m_PendingWorkers = static_cast<uint32_t>(m_vThreads.size());
                ++m_Generation;
            }
            m_CvWork.notify_all();

            executeBatches(0u);

            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CvDone.wait(lock, [this]()
                              {
                                  return m_PendingWorkers == 0;
                              });
                m_Job = nullptr;
            }

            if (capturedException)
            {
                std::rethrow_exception(capturedException);
            }
        }

    private:
        void WorkerLoop(uint32_t workerIndex)
        {
            uint64_t seenGeneration = 0;
            while (true)
            {
                std::function<void(uint32_t)> localJob;
                {
                    std::unique_lock<std::mutex> lock(m_Mutex);
                    m_CvWork.wait(lock, [this, &seenGeneration]()
                                  {
                                      return m_Stopping || m_Generation != seenGeneration;
                                  });
                    if (m_Stopping)
                    {
                        return;
                    }
                    seenGeneration = m_Generation;
                    localJob = m_Job;
                }

                if (localJob)
                {
                    localJob(workerIndex);
                }

                {
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    if (m_PendingWorkers > 0)
                    {
                        --m_PendingWorkers;
                        if (m_PendingWorkers == 0)
                        {
                            m_CvDone.notify_one();
                        }
                    }
                }
            }
        }

        uint32_t m_WorkerCount = 1;
        std::vector<std::thread> m_vThreads;
        std::mutex m_Mutex;
        std::condition_variable m_CvWork;
        std::condition_variable m_CvDone;
        uint64_t m_Generation = 0;
        uint32_t m_PendingWorkers = 0;
        bool m_Stopping = false;
        std::atomic<size_t> m_NextBatch{0};
        std::atomic<uint64_t> m_CompletedBatches{0};
        std::function<void(uint32_t)> m_Job;
    };
} // qci

#endif // __QUARK_RENDER_THREAD_POOL__
