/**
 * @file PositionManager.hpp
 * @brief 单只股票持仓管理器
 *
 * 层级：
 *   backend/calcFactors/runtime/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/runtime/PositionManager.hpp
 *
 * 模块作用：
 *   管理单只股票的持仓数量、成本、浮动盈亏与库存状态。
 *
 * 使用者：
 *   PortfolioManager 通过本类管理每个标的的持仓。
 *
 * 项目角色：
 *   C++ 运行时层的持仓管理模块，是交易状态维护的基本单元。
 *
 * 引入说明：
 *   依赖 config/Kline.hpp。
 *   依赖标准库 cstdint、vector。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include "config/Kline.hpp"
#include <cstdint>
#include <vector>

namespace runtime {

/**
 * @brief 单只股票持仓状态
 *
 * 记录持仓数量、成本、现价、浮动盈亏和库存状态。
 */
struct Position {
    uint32_t code = 0;
    int64_t quantity = 0;             // 持仓数量
    double avgCost = 0.0;             // 平均成本
    double currentPrice = 0.0;        // 最新价
    double floatingPnl = 0.0;         // 浮动盈亏
    int64_t inventoryAvailable = 0;   // 可用库存
    int64_t inventoryFrozen = 0;      // 冻结库存
};

/**
 * @brief 单只股票持仓管理器
 *
 * 负责开仓、平仓、盯市和库存状态维护。
 */
class PositionManager {
public:
    /**
     * @brief 构造持仓管理器
     * @param code 股票代码
     */
    explicit PositionManager(uint32_t code) : pos_{} {
        pos_.code = code;
    }

    /**
     * @brief 开仓
     * @param qty 买入数量
     * @param price 成交价
     * @note 按成交量重新计算平均成本
     */
    void open(int64_t qty, double price) {
        double totalCost = pos_.avgCost * pos_.quantity + qty * price;
        pos_.quantity += qty;
        if (pos_.quantity > 0) {
            pos_.avgCost = totalCost / pos_.quantity;
        }
        pos_.inventoryAvailable = pos_.quantity;
    }

    /**
     * @brief 平仓
     * @param qty 卖出数量
     * @param price 成交价
     * @note 若卖出数量超过持仓，则只卖出当前持仓数量
     */
    void close(int64_t qty, double price) {
        if (qty > pos_.quantity) qty = pos_.quantity;
        pos_.quantity -= qty;
        if (pos_.quantity == 0) {
            pos_.avgCost = 0.0;
        }
        pos_.inventoryAvailable = pos_.quantity;
    }

    /**
     * @brief 更新最新价格并计算浮动盈亏
     * @param price 最新成交价
     */
    void markToMarket(double price) {
        pos_.currentPrice = price;
        pos_.floatingPnl = (price - pos_.avgCost) * pos_.quantity;
    }

    /**
     * @brief 获取当前持仓状态
     * @return 持仓结构体常量引用
     */
    const Position& get() const { return pos_; }

private:
    Position pos_;
};

} // namespace runtime