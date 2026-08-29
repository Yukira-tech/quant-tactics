/**
 * @file Clock.hpp
 * @brief 时间驱动时钟
 *
 * 层级：
 *   backend/calcFactors/runtime/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/runtime/Clock.hpp
 *
 * 模块作用：
 *   提供实时与回放两种时间驱动模式。
 *   实时模式按固定间隔自动触发回调；回放模式由外部手动 tick。
 *
 * 使用者：
 *   main.cpp 通过本类驱动全市场因子计算与信号更新。
 *
 * 项目角色：
 *   C++ 运行时层的时间调度基座。
 *
 * 引入说明：
 *   依赖标准库 chrono、thread、functional。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include <chrono>
#include <thread>
#include <functional>

namespace runtime {

/**
 * @brief 时间驱动时钟
 *
 * 支持实时模式和回放模式。实时模式内部循环触发，回放模式由外部控制。
 */
class Clock {
public:
    using TickCallback = std::function<void()>;

    /**
     * @brief 构造时钟
     * @param intervalMs 每次 tick 间隔，单位毫秒
     */
    explicit Clock(int intervalMs = 1000)
        : intervalMs_(intervalMs), running_(false) {}

    /**
     * @brief 设置 tick 回调
     * @param cb 每次 tick 时调用的函数
     */
    void setCallback(TickCallback cb) {
        callback_ = std::move(cb);
    }

    /**
     * @brief 启动实时模式
     * @note 按固定间隔循环触发回调，直到 stop() 被调用
     */
    void startRealtime() {
        running_ = true;
        while (running_) {
            if (callback_) callback_();
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs_));
        }
    }

    /**
     * @brief 停止时钟
     */
    void stop() {
        running_ = false;
    }

    /**
     * @brief 手动触发一次 tick
     * @note 回放模式使用
     */
    void tickOnce() {
        if (callback_) callback_();
    }

private:
    int intervalMs_;          // tick 间隔，毫秒
    bool running_;            // 实时模式运行标志
    TickCallback callback_;   // tick 回调
};

} // namespace runtime
