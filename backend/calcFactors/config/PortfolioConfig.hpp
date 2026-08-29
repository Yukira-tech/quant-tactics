/**
 * @file PortfolioConfig.hpp
 * @brief 组合管理配置
 *
 * 层级：
 *   backend/calcFactors/config/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/config/PortfolioConfig.hpp
 *
 * 模块作用：
 *   定义组合层面的资金、仓位约束与交易成本参数。
 *
 * 使用者：
 *   PortfolioManager 读取本配置计算总权益与目标组合。
 *   PortfolioOptimizer 依据本配置约束仓位。
 *
 * 项目角色：
 *   C++ 组合管理模块的配置基座。
 *
 * 引入说明：
 *   仅依赖标准库 cstddef，提供 size_t 类型。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 统一文件头
 */

#pragma once

#include <cstddef>

namespace config {

/**
 * @brief 组合管理配置
 *
 * 字段包括初始资金、单票仓位上限、最大持仓数、手续费率和滑点。
 */
struct PortfolioConfig {
    double initialCapital = 1'000'000.0;  // 初始资金
    double maxPositionRatio = 0.2;        // 单票最大仓位比例
    size_t maxPositions = 10;             // 最大持仓股票数
    double feeRate = 0.001;               // 手续费率
    double slippageRate = 0.0005;         // 滑点比例
    bool useFixedSlippage = true;         // 是否使用固定比例滑点

    /**
     * @brief 校验配置合法性
     * @return true 表示所有参数合法；false 表示存在非法参数
     */
    bool validate() const {
        if (initialCapital <= 0) return false;
        if (maxPositionRatio <= 0.0 || maxPositionRatio > 1.0) return false;
        if (maxPositions == 0) return false;
        if (feeRate < 0.0 || slippageRate < 0.0) return false;
        return true;
    }
};

} // namespace config