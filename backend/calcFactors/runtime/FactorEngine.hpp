/**
 * @file FactorEngine.hpp
 * @brief 因子计算引擎
 *
 * 层级：
 *   backend/calcFactors/runtime/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/runtime/FactorEngine.hpp
 *
 * 模块作用：
 *   接收逐根K线，滚动计算双均线、唐奇安通道、ATR 等技术因子。
 *
 * 使用者：
 *   StrategyEngine 依赖本模块输出的因子生成交易信号。
 *
 * 项目角色：
 *   C++ 信号链路的计算起点，所有技术指标在此产出。
 *
 * 引入说明：
 *   依赖 config/Kline.hpp 和 config/StrategyConfig.hpp。
 *   依赖标准库 deque、algorithm、numeric、cmath。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include <deque>
#include <algorithm>
#include <numeric>
#include <cmath>

#include "config/Kline.hpp"
#include "config/StrategyConfig.hpp"

namespace runtime {

/**
 * @brief 因子计算结果
 *
 * 汇总一根K线对应的全部技术因子值。
 */
struct FactorResult {
    double maShort = 0.0;         // 短期均线
    double maLong  = 0.0;         // 长期均线
    double donchianHigh = 0.0;    // 唐奇安上轨
    double donchianLow  = 0.0;    // 唐奇安下轨
    double atr = 0.0;             // ATR 值
    double close = 0.0;           // 当前收盘价
};

/**
 * @brief 因子计算引擎
 *
 * 内部维护滚动窗口，每调用一次 update() 就根据最新K线更新所有因子。
 */
class FactorEngine {
public:
    /**
     * @brief 构造因子引擎
     * @param cfg 策略配置，决定各因子的窗口大小
     */
    explicit FactorEngine(const config::StrategyConfig& cfg)
        : cfg_(cfg), prevClose_(0.0) {}

    /**
     * @brief 输入新K线，计算最新因子
     * @param k 最新K线
     * @return 最新因子计算结果
     */
    FactorResult update(const config::Kline& k) {
        closes_.push_back(k.close);
        highs_.push_back(k.high);
        lows_.push_back(k.low);

        trimWindows();

        // 计算真实波幅并压入 ATR 窗口
        double tr = calcTrueRange(k);
        trs_.push_back(tr);
        if (trs_.size() > cfg_.atrWindow) {
            trs_.pop_front();
        }
        prevClose_ = k.close;

        FactorResult res;
        res.close = k.close;

        res.maShort = calcSMA(closes_, cfg_.shortWindow);
        res.maLong  = calcSMA(closes_, cfg_.longWindow);

        if (highs_.size() >= cfg_.donchianWindow) {
            res.donchianHigh = *std::max_element(highs_.begin(), highs_.end());
            res.donchianLow  = *std::min_element(lows_.begin(), lows_.end());
        }

        res.atr = calcSMA(trs_, cfg_.atrWindow);

        return res;
    }

    /**
     * @brief 重置所有内部状态
     * @note 清空所有滚动窗口和上一根收盘价
     */
    void reset() {
        closes_.clear();
        highs_.clear();
        lows_.clear();
        trs_.clear();
        prevClose_ = 0.0;
    }

private:
    // 控制窗口大小，只保留最长因子窗口所需的数量
    void trimWindows() {
        size_t maxWin = std::max(cfg_.longWindow, cfg_.donchianWindow);
        maxWin = std::max(maxWin, cfg_.atrWindow);
        if (closes_.size() > maxWin) closes_.pop_front();
        if (highs_.size() > maxWin)  highs_.pop_front();
        if (lows_.size() > maxWin)   lows_.pop_front();
    }

    // 首根K线 TR 用 high-low，其余用三值最大值
    double calcTrueRange(const config::Kline& k) const {
        if (prevClose_ == 0.0) {
            return k.high - k.low;
        }
        return std::max({
            k.high - k.low,
            std::abs(k.high - prevClose_),
            std::abs(k.low - prevClose_)
        });
    }

    // 窗口不足时返回 0，避免因子计算出现未定义行为
    static double calcSMA(const std::deque<double>& data, size_t window) {
        if (data.size() < window || window == 0) return 0.0;
        auto end = data.end();
        auto start = end - window;
        double sum = std::accumulate(start, end, 0.0);
        return sum / static_cast<double>(window);
    }

private:
    config::StrategyConfig cfg_;
    std::deque<double> closes_;   // 收盘价窗口
    std::deque<double> highs_;    // 最高价窗口
    std::deque<double> lows_;     // 最低价窗口
    std::deque<double> trs_;      // 真实波幅窗口
    double prevClose_;            // 上一根收盘价
};

} // namespace runtime