/**
 * @file PortfolioManager.hpp
 * @brief 全局组合管理器
 *
 * 层级：
 *   backend/calcFactors/runtime/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/runtime/PortfolioManager.hpp
 *
 * 模块作用：
 *   聚合所有股票的持仓与信号，维护全局账户状态，
 *   并调用组合优化器生成目标持仓。
 *
 * 使用者：
 *   main.cpp 通过本类管理全市场组合。
 *
 * 项目角色：
 *   C++ 运行时层的顶层组合管理模块，是持仓与信号的汇聚点。
 *
 * 引入说明：
 *   依赖 config/PortfolioConfig.hpp、runtime/PositionManager.hpp、
 *   runtime/StrategyEngine.hpp、runtime/PortfolioOptimizer.hpp。
 *   依赖标准库 map、memory。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include "config/PortfolioConfig.hpp"
#include "runtime/PositionManager.hpp"
#include "runtime/StrategyEngine.hpp"
#include "runtime/PortfolioOptimizer.hpp"
#include <map>
#include <memory>

namespace runtime {

/**
 * @brief 全局组合管理器
 *
 * 管理所有标的的持仓与信号，并维护总权益。
 */
class PortfolioManager {
public:
    /**
     * @brief 构造组合管理器
     * @param cfg 组合配置
     */
    PortfolioManager(const config::PortfolioConfig& cfg)
        : cfg_(cfg), optimizer_(cfg), totalEquity_(cfg.initialCapital) {}

    /**
     * @brief 更新某只股票的信号与持仓
     * @param code 股票代码
     * @param sig  最新信号
     * @param price 当前价格
     * @note 当前为简化逻辑：买入固定 100 股，卖出全部持仓
     */
    void updatePosition(uint32_t code, const SignalResult& sig, double price) {
        if (positions_.find(code) == positions_.end()) {
            positions_.emplace(code, std::make_unique<PositionManager>(code));
        }

        auto& pm = positions_[code];
        if (sig.type == SignalType::Buy && pm->get().quantity == 0) {
            pm->open(100, price);  // 简化：固定买100股
        } else if (sig.type == SignalType::Sell && pm->get().quantity > 0) {
            pm->close(pm->get().quantity, price);
        }

        pm->markToMarket(price);
        signals_[code] = sig;
    }

    /**
     * @brief 计算账户总权益
     * @return 初始资金加全部持仓浮动盈亏
     */
    double calcTotalEquity() const {
        double total = cfg_.initialCapital;
        for (const auto& [code, pm] : positions_) {
            total += pm->get().floatingPnl;
        }
        return total;
    }

    /**
     * @brief 生成目标组合
     * @return key 为股票代码，value 为目标持仓数量
     */
    std::map<uint32_t, int64_t> optimize() {
        totalEquity_ = calcTotalEquity();
        return optimizer_.optimize(signals_, totalEquity_);
    }

private:
    config::PortfolioConfig cfg_;
    PortfolioOptimizer optimizer_;
    std::map<uint32_t, std::unique_ptr<PositionManager>> positions_;
    std::map<uint32_t, SignalResult> signals_;
    double totalEquity_;
};

} // namespace runtime