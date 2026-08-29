/**
 * @file StrategyConfig.hpp
 * @brief 策略参数配置
 *
 * 层级：
 *   backend/calcFactors/config/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/config/StrategyConfig.hpp
 *
 * 模块作用：
 *   集中管理双均线与 CTA 策略的全部可调参数。
 *
 * 使用者：
 *   FactorEngine 读取窗口参数。
 *   StrategyEngine 读取策略开关与优先级。
 *
 * 项目角色：
 *   C++ 策略引擎的参数基座，改变策略行为不碰代码，只改配置。
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
 * @brief 策略参数配置
 *
 * 集中管理双均线与 CTA 的可调参数。
 */
struct StrategyConfig {
    // 双均线
    size_t shortWindow = 5;    // 快线窗口
    size_t longWindow  = 20;   // 慢线窗口

    // CTA
    size_t donchianWindow = 20;   // 唐奇安通道窗口
    size_t atrWindow      = 14;   // ATR 窗口
    double atrStopMultiple = 2.0; // ATR 止损倍数

    // 策略开关
    bool enableDualMA = true;   // 启用双均线
    bool enableCTA    = true;   // 启用 CTA

    // CTA 信号优先于双均线
    bool ctaPriority = true;

    /**
     * @brief 校验配置合法性
     * @return true 表示所有参数合法；false 表示存在非法参数
     */
    bool validate() const {
        if (shortWindow == 0 || longWindow == 0) return false;
        if (shortWindow >= longWindow) return false;
        if (donchianWindow == 0 || atrWindow == 0) return false;
        if (atrStopMultiple <= 0.0) return false;
        return true;
    }
};

} // namespace config