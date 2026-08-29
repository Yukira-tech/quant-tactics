/**
 * @file PortfolioOptimizer.hpp
 * @brief 组合优化器
 *
 * 层级：
 *   backend/calcFactors/runtime/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/runtime/PortfolioOptimizer.hpp
 *
 * 模块作用：
 *   根据股票信号强度与组合配置，贪心选择目标持仓组合。
 *
 * 使用者：
 *   PortfolioManager 调用本类生成目标持仓。
 *
 * 项目角色：
 *   C++ 运行时层的组合决策模块，决定资金在不同标的间的分配。
 *
 * 引入说明：
 *   依赖 config/PortfolioConfig.hpp、runtime/PositionManager.hpp、
 *   runtime/StrategyEngine.hpp。
 *   依赖标准库 map、vector、algorithm。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include "config/PortfolioConfig.hpp"
#include "runtime/PositionManager.hpp"
#include "runtime/StrategyEngine.hpp"
#include <map>
#include <vector>
#include <algorithm>

namespace runtime {

/**
 * @brief 组合优化器
 *
 * MVP 阶段使用贪心策略：按信号强度排序，依次分配仓位。
 * 后续可替换为均值方差等更完整的优化模型。
 */
class PortfolioOptimizer {
public:
    /**
     * @brief 构造组合优化器
     * @param cfg 组合配置
     */
    PortfolioOptimizer(const config::PortfolioConfig& cfg) : cfg_(cfg) {}

    /**
     * @brief 根据信号强度贪心选择目标持仓
     * @param signals 各股票信号
     * @param totalEquity 当前总权益
     * @return key 为股票代码，value 为目标持仓数量
     * @note 当前简化为假设股价 10 元，后续应替换为真实价格计算
     */
    std::map<uint32_t, int64_t> optimize(
        const std::map<uint32_t, SignalResult>& signals,
        double totalEquity
    ) {
        std::vector<std::pair<uint32_t, double>> ranked;
        for (const auto& [code, sig] : signals) {
            if (sig.type == SignalType::Buy) {
                ranked.emplace_back(code, sig.strength);
            }
        }

        // 按信号强度降序排列
        std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        std::map<uint32_t, int64_t> target;
        double perStock = totalEquity * cfg_.maxPositionRatio;
        size_t count = 0;

        for (const auto& [code, strength] : ranked) {
            if (count >= cfg_.maxPositions) break;
            target[code] = static_cast<int64_t>(perStock / 10.0); // 假设价格10元
            ++count;
        }

        return target;
    }

private:
    config::PortfolioConfig cfg_;
};

} // namespace runtime