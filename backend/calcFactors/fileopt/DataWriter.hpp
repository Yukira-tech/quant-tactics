/**
 * @file DataWriter.hpp
 * @brief 数据写出器：将 C++ 数据序列化为 JSON 并原子写入运行时文件
 *
 * 层级：
 *   backend/calcFactors/fileopt/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/fileopt/DataWriter.hpp
 *
 * 模块作用：
 *   提供 writeJSON 与 writeFactor / writeSignal / writePosition / writePortfolio
 *   等业务写入接口，底层统一走 RuntimeFileManager 的原子写。
 *
 * 使用者：
 *   runtime 层通过本类把因子、信号、持仓、组合快照写入 runtime_files/。
 *
 * 项目角色：
 *   C++ 侧向运行时文件目录输出的统一出口。
 *
 * 引入说明：
 *   依赖 RuntimeFileManager.hpp。
 *   依赖标准库 string、cstdint。
 *
 * 维护记录：
 *   2026-08-28 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include "RuntimeFileManager.hpp"

#include <string>
#include <cstdint>

namespace fileopt {

/**
 * @brief 纯静态数据写出器，禁止实例化
 *
 * 负责把 C++ 业务对象手工拼接为 JSON 字符串，再通过 RuntimeFileManager 原子落盘。
 * 后续如有需要，可把手工拼接替换为 nlohmann/json 等库。
 */
class DataWriter {
public:
    DataWriter() = delete;

    /**
     * @brief 原子写入 JSON 字符串
     * @param filepath 目标文件完整路径
     * @param json 已序列化的 JSON 字符串
     * @return true 写入成功；false 写入失败
     */
    static bool writeJSON(const std::string& filepath, const std::string& json) {
#if CPPSTD_HAS_CPP17
        return RuntimeFileManager::AtomicWrite(
            std::filesystem::path(filepath),
            json
        );
#else
        return RuntimeFileManager::AtomicWrite(
            filepath.c_str(),
            json.c_str()
        );
#endif
    }

#if CPPSTD_HAS_CPP17
    /**
     * @brief 原子写入 JSON 字符串（fs::path 重载）
     * @param filepath std::filesystem 路径对象
     * @param json 已序列化的 JSON 字符串
     * @return true 写入成功；false 写入失败
     */
    static bool writeJSON(const std::filesystem::path& filepath, const std::string& json) {
        return RuntimeFileManager::AtomicWrite(filepath, json);
    }
#endif

    /**
     * @brief 生成运行时文件完整路径
     * @param baseDir 运行时根目录，如 "runtime_files"
     * @param subDir  子目录，如 "factor_outputs"
     * @param symbol  股票代码，如 "600000"
     * @param ext     扩展名，默认 ".json"
     * @return 完整路径字符串
     */
    static std::string makePath(
        const std::string& baseDir,
        const std::string& subDir,
        const std::string& symbol,
        const std::string& ext = ".json"
    ) {
        return baseDir + "/" + subDir + "/" + symbol + ext;
    }

    /**
     * @brief 写入因子数据
     * @param symbol 股票代码
     * @param timestamp 时间戳字符串
     * @param maShort 短期均线
     * @param maLong 长期均线
     * @param donchianHigh 唐奇安上轨
     * @param donchianLow 唐奇安下轨
     * @param atr ATR 值
     * @return true 写入成功
     */
    static bool writeFactor(
        const std::string& symbol,
        const std::string& timestamp,
        double maShort,
        double maLong,
        double donchianHigh,
        double donchianLow,
        double atr
    ) {
        std::string json = 
            "{\"symbol\":\"" + symbol + "\","
            "\"timestamp\":\"" + timestamp + "\","
            "\"factors\":{"
                "\"ma_short\":" + std::to_string(maShort) + ","
                "\"ma_long\":" + std::to_string(maLong) + ","
                "\"donchian_high\":" + std::to_string(donchianHigh) + ","
                "\"donchian_low\":" + std::to_string(donchianLow) + ","
                "\"atr\":" + std::to_string(atr) +
            "}}";

        std::string path = makePath("runtime_files", "factor_outputs", symbol);
        return writeJSON(path, json);
    }

    /**
     * @brief 写入交易信号
     * @param symbol 股票代码
     * @param timestamp 时间戳字符串
     * @param signal 信号枚举值，如 "buy" / "sell" / "hold"
     * @param strength 信号强度，范围 0.0~1.0
     * @param source 策略来源，如 "DualMA" / "CTA"
     * @return true 写入成功
     */
    static bool writeSignal(
        const std::string& symbol,
        const std::string& timestamp,
        const std::string& signal,
        double strength,
        const std::string& source
    ) {
        std::string json = 
            "{\"symbol\":\"" + symbol + "\","
            "\"timestamp\":\"" + timestamp + "\","
            "\"signal\":\"" + signal + "\","
            "\"strength\":" + std::to_string(strength) + ","
            "\"strategy_source\":\"" + source + "\"}";

        std::string path = makePath("runtime_files", "signals", symbol);
        return writeJSON(path, json);
    }

    /**
     * @brief 写入持仓状态
     * @param symbol 股票代码
     * @param timestamp 时间戳字符串
     * @param quantity 持仓数量
     * @param avgCost 平均成本
     * @param currentPrice 当前价格
     * @param floatingPnl 浮动盈亏
     * @param inventoryAvailable 可用库存
     * @param inventoryFrozen 冻结库存
     * @return true 写入成功
     */
    static bool writePosition(
        const std::string& symbol,
        const std::string& timestamp,
        uint32_t quantity,
        double avgCost,
        double currentPrice,
        double floatingPnl,
        uint32_t inventoryAvailable,
        uint32_t inventoryFrozen
    ) {
        std::string json = 
            "{\"symbol\":\"" + symbol + "\","
            "\"timestamp\":\"" + timestamp + "\","
            "\"quantity\":" + std::to_string(quantity) + ","
            "\"avg_cost\":" + std::to_string(avgCost) + ","
            "\"current_price\":" + std::to_string(currentPrice) + ","
            "\"floating_pnl\":" + std::to_string(floatingPnl) + ","
            "\"inventory_available\":" + std::to_string(inventoryAvailable) + ","
            "\"inventory_frozen\":" + std::to_string(inventoryFrozen) + "}";

        std::string path = makePath("runtime_files", "positions", symbol);
        return writeJSON(path, json);
    }

    /**
     * @brief 写入组合快照
     * @param timestamp 时间戳字符串
     * @param totalEquity 总权益
     * @param totalPnl 总盈亏
     * @param maxDrawdown 最大回撤，如 0.02 表示 2%
     * @param targetPortfolioJson 目标组合的 JSON 数组字符串
     * @return true 写入成功
     */
    static bool writePortfolio(
        const std::string& timestamp,
        double totalEquity,
        double totalPnl,
        double maxDrawdown,
        const std::string& targetPortfolioJson
    ) {
        std::string json = 
            "{\"timestamp\":\"" + timestamp + "\","
            "\"total_equity\":" + std::to_string(totalEquity) + ","
            "\"total_pnl\":" + std::to_string(totalPnl) + ","
            "\"max_drawdown\":" + std::to_string(maxDrawdown) + ","
            "\"target_portfolio\":" + targetPortfolioJson + "}";

        std::string path = "runtime_files/portfolio/snapshot.json";
        return writeJSON(path, json);
    }
};

} // namespace fileopt