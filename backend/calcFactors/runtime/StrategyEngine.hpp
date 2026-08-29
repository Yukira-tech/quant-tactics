/**
 * @file StrategyEngine.hpp
 * @brief 策略引擎：将因子转化为交易信号
 *
 * 层级：
 *   backend/calcFactors/runtime/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/runtime/StrategyEngine.hpp
 *
 * 模块作用：
 *   接收 FactorEngine 产出的因子，生成双均线与 CTA 交易信号。
 *   支持 CTA 优先级高于双均线的可配置策略。
 *
 * 使用者：
 *   PortfolioManager 通过本模块获取最新交易信号。
 *   main.cpp 通过本模块驱动信号生成。
 *
 * 项目角色：
 *   C++ 运行时层的信号决策模块，是规则策略的核心实现。
 *
 * 引入说明：
 *   依赖 config/StrategyConfig.hpp 和 runtime/FactorEngine.hpp。
 *   依赖标准库 string。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include "config/StrategyConfig.hpp"
#include "runtime/FactorEngine.hpp"
#include <string>

namespace runtime {

/**
 * @brief 交易信号类型
 */
enum class SignalType {
    Hold,
    Buy,
    Sell,
    Short,
    Cover
};

/**
 * @brief 信号结果
 */
struct SignalResult {
    SignalType type = SignalType::Hold;
    double strength = 0.0;          // 信号强度 0~1
    std::string strategySource;     // 信号来源
};

/**
 * @brief 策略引擎
 *
 * 内部维护一个 FactorEngine，每次 update 时先算因子再生成信号。
 */
class StrategyEngine {
public:
    /**
     * @brief 构造策略引擎
     * @param cfg 策略配置
     */
    explicit StrategyEngine(const config::StrategyConfig& cfg)
        : cfg_(cfg), factorEngine_(cfg), prevShort_(0.0), prevLong_(0.0), initialized_(false) {}

    /**
     * @brief 更新K线并返回交易信号
     * @param k 最新K线
     * @return 最新信号结果
     * @note 默认 CTA 信号优先于双均线信号
     */
    SignalResult update(const config::Kline& k) {
        FactorResult f = factorEngine_.update(k);

        SignalResult dualMA = dualMASignal(f);
        SignalResult cta = ctaSignal(f);

        if (cfg_.enableCTA && cfg_.ctaPriority && cta.type != SignalType::Hold) {
            return cta;
        }
        if (cfg_.enableDualMA && dualMA.type != SignalType::Hold) {
            return dualMA;
        }
        if (cfg_.enableCTA) return cta;
        if (cfg_.enableDualMA) return dualMA;

        return SignalResult{};
    }

    /**
     * @brief 重置策略状态
     */
    void reset() {
        factorEngine_.reset();
        prevShort_ = 0.0;
        prevLong_ = 0.0;
        initialized_ = false;
    }

private:
    // 金叉买入，死叉卖出；首次只记录不产生信号
    SignalResult dualMASignal(const FactorResult& f) {
        SignalResult res;
        res.strategySource = "DualMA";

        if (!initialized_) {
            prevShort_ = f.maShort;
            prevLong_ = f.maLong;
            initialized_ = true;
            return res;
        }

        if (prevShort_ <= prevLong_ && f.maShort > f.maLong) {
            res.type = SignalType::Buy;
            res.strength = 0.8;
        } else if (prevShort_ >= prevLong_ && f.maShort < f.maLong) {
            res.type = SignalType::Sell;
            res.strength = 0.8;
        }

        prevShort_ = f.maShort;
        prevLong_ = f.maLong;
        return res;
    }

    // 突破唐奇安上轨做多，跌破下轨做空
    SignalResult ctaSignal(const FactorResult& f) {
        SignalResult res;
        res.strategySource = "CTA";

        if (f.donchianHigh == 0.0) return res;

        if (f.close > f.donchianHigh) {
            res.type = SignalType::Buy;
            res.strength = 0.9;
        } else if (f.close < f.donchianLow) {
            res.type = SignalType::Sell;
            res.strength = 0.9;
        }

        return res;
    }

private:
    config::StrategyConfig cfg_;
    FactorEngine factorEngine_;
    double prevShort_;
    double prevLong_;
    bool initialized_;
};

} // namespace runtime