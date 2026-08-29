/**
 * @file Kline.hpp
 * @brief K线数据结构（64 字节缓存行对齐）
 *
 * 层级：
 *   backend/calcFactors/config/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/config/Kline.hpp
 *
 * 模块作用：
 *   定义单根K线的内存结构，固定 64 字节大小，
 *   保证连续遍历时单根K线不会跨两条 CPU Cache Line。
 *
 * 使用者：
 *   DataLoader 加载历史K线后存入 std::vector<Kline>。
 *   FactorEngine 遍历 Kline 序列计算技术因子。
 *   runtime 层各模块均依赖本结构。
 *
 * 项目角色：
 *   C++ 因子计算链路的底层数据结构，是性能敏感的基座。
 *
 * 引入说明：
 *   仅依赖标准库 cstdint 和 cstddef，无第三方依赖。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-28 改为 64 字节缓存行对齐，新增 padding
 *   2026-08-29 按 docs/COMMENT_STYLE.md 统一文件头
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace config {

/**
 * @brief 缓存行对齐的 K 线结构体
 *
 * 字段布局（64 位系统标准 ABI）：
 *
 * | 字段 | 大小 | 偏移 | 说明 |
 * |------|------|------|------|
 * | code | 4 | 0 | 股票代码 |
 * | ymd | 4 | 4 | 年月日整数 |
 * | open | 8 | 8 | 开盘价 |
 * | high | 8 | 16 | 最高价 |
 * | low | 8 | 24 | 最低价 |
 * | close | 8 | 32 | 收盘价 |
 * | volume | 8 | 40 | 成交量 |
 * | padding | 16 | 48 | 手动填充至 64 字节 |
 *
 * 排序语义：先按股票代码，再按日期。同一只股票的历史K线会聚在一起按时间排列。
 * padding 仅用于对齐，禁止业务读写。
 */
struct alignas(64) Kline {
    uint32_t code = 0;      // 股票代码，6 位 A 股代码
    int32_t  ymd = 0;       // 年月日整数，如 20240115

    double open  = 0.0;
    double high  = 0.0;
    double low   = 0.0;
    double close = 0.0;
    int64_t volume = 0;

    uint8_t padding[16];    // 手动填充，不参与业务逻辑

    // ---------------- 比较运算符 ----------------

    // 同一只股票同一天视为同一根 K 线
    bool operator==(const Kline& other) const {
        return code == other.code && ymd == other.ymd;
    }

    bool operator!=(const Kline& other) const {
        return !(*this == other);
    }

    // 先按股票代码排序，再按日期，多股票混排时同一只股票聚在一起
    bool operator<(const Kline& other) const {
        if (code != other.code) return code < other.code;
        return ymd < other.ymd;
    }

    bool operator>(const Kline& other) const {
        return other < *this;
    }

    bool operator<=(const Kline& other) const {
        return !(*this > other);
    }

    bool operator>=(const Kline& other) const {
        return !(*this < other);
    }
};

// 编译期断言，确保结构体大小和对齐始终为 64 字节
static_assert(sizeof(Kline) == 64, "Kline must be exactly 64 bytes for cache line alignment");
static_assert(alignof(Kline) == 64, "Kline must be 64-byte aligned");

} // namespace config