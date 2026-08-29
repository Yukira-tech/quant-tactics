/**
 * @file ThreadPool.hpp
 * @brief 线程池
 *
 * 层级：
 *   backend/calcFactors/utils/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/utils/ThreadPool.hpp
 *
 * 模块作用：
 *   维护固定数量工作线程与任务队列，支持任务入队和安全关闭。
 *   支持移动语义，禁止拷贝。
 *
 * 使用者：
 *   DataLoader 通过本类并行加载多只股票历史K线。
 *   后续其他需要并行计算的模块也可复用。
 *
 * 项目角色：
 *   C++ 并发基础设施，为多任务并行执行提供统一调度。
 *
 * 引入说明：
 *   依赖标准库 thread、vector、queue、mutex、condition_variable、
 *   functional、stdexcept、algorithm、atomic、memory。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-28 增加移动语义，stop_ 改为原子操作
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <stdexcept>
#include <algorithm>
#include <atomic>
#include <memory>

namespace utils {

/**
 * @brief 线程池
 *
 * 使用有锁任务队列 + 条件变量实现。支持移动，禁止拷贝。
 * mutex 和 condition_variable 不可移动，因此用 unique_ptr 包装。
 */
class ThreadPool {
public:
    using Task = std::function<void()>;

    /**
     * @brief 构造线程池并启动工作线程
     * @param numThreads 工作线程数量
     */
    explicit ThreadPool(size_t numThreads)
        : queueMutex_(std::make_unique<std::mutex>()),
          cv_(std::make_unique<std::condition_variable>()),
          stop_(false)
    {
        workers_.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back(&ThreadPool::WorkerLoop, this);
        }
    }

    /**
     * @brief 析构并关闭线程池
     */
    ~ThreadPool() { ShutDown(); }

    /**
     * @brief 移动构造
     * @param rhs 源对象，转移后源对象不可再使用
     */
    ThreadPool(ThreadPool&& rhs) noexcept
        : workers_(std::move(rhs.workers_)),
          tasks_(std::move(rhs.tasks_)),
          queueMutex_(std::move(rhs.queueMutex_)),
          cv_(std::move(rhs.cv_)),
          stop_(rhs.stop_.load(std::memory_order_relaxed))
    {
        // 源对象的 unique_ptr 已置空，stop_ 状态已复制
    }

    /**
     * @brief 移动赋值
     * @param rhs 源对象
     * @return 本对象引用
     * @note 先关闭自身线程，再转移源对象所有权
     */
    ThreadPool& operator=(ThreadPool&& rhs) noexcept {
        if (this != &rhs) {
            ShutDown();

            workers_ = std::move(rhs.workers_);
            tasks_ = std::move(rhs.tasks_);
            queueMutex_ = std::move(rhs.queueMutex_);
            cv_ = std::move(rhs.cv_);
            stop_.store(rhs.stop_.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
        }
        return *this;
    }

    // 禁止拷贝
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief 提交任务到线程池
     * @param task 可调用对象，无返回值
     * @throws std::runtime_error 线程池已关闭时抛出
     */
    void Enqueue(Task task) {
        {
            std::lock_guard<std::mutex> lock(*queueMutex_);
            if (stop_.load(std::memory_order_acquire)) {
                throw std::runtime_error(
                    "ThreadPool has been stopped, cannot enqueue new tasks."
                );
            }
            tasks_.push(std::move(task));
        }
        cv_->notify_one();
    }

    /**
     * @brief 关闭线程池
     * @note 唤醒所有工作线程并等待其退出，清空任务队列
     */
    void ShutDown() {
        {
            std::lock_guard<std::mutex> lock(*queueMutex_);
            if (stop_.load(std::memory_order_acquire)) return;
            stop_.store(true, std::memory_order_release);
        }

        cv_->notify_all();
        for (std::thread& t : workers_) {
            if (t.joinable()) t.join();
        }

        std::queue<Task> empty;
        tasks_.swap(empty);  // 清空队列
    }

private:
    // 工作线程主循环：等待任务，取出任务，执行任务
    void WorkerLoop() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(*queueMutex_);
                cv_->wait(lock, [this] {
                    return stop_.load(std::memory_order_acquire) || !tasks_.empty();
                });

                if (stop_.load(std::memory_order_acquire) && tasks_.empty()) {
                    return;
                }

                task = std::move(tasks_.front());
                tasks_.pop();
            }

            if (task) task();
        }
    }

private:
    std::vector<std::thread> workers_;               // 工作线程集合
    std::queue<Task> tasks_;                         // 任务队列
    std::unique_ptr<std::mutex> queueMutex_;         // 队列锁
    std::unique_ptr<std::condition_variable> cv_;    // 条件变量
    std::atomic<bool> stop_{false};                  // 关闭标志
};

} // namespace utils
