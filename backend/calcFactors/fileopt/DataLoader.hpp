/**
 * @file DataLoader.hpp
 * @brief 数据加载器：从磁盘批量读取历史K线
 *
 * 层级：
 *   backend/calcFactors/fileopt/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/fileopt/DataLoader.hpp
 *
 * 模块作用：
 *   提供单文件与多文件并行加载历史K线的能力。
 *   内部维护线程池，逐行解析 JSONL 格式的K线文件。
 *
 * 使用者：
 *   runtime 层与 main.cpp 通过本类加载历史行情数据。
 *
 * 项目角色：
 *   C++ 数据链路入口，不参与因子计算、策略判断或数据写出。
 *
 * 引入说明：
 *   依赖 FileGuard、config/Kline、utils/ThreadPool。
 *   依赖标准库 optional、string_view、vector、string、map、mutex、
 *   condition_variable、iostream。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include <optional>
#include <string_view>
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <condition_variable>
#include <iostream>

#include "FileGuard.hpp"
#include "config/Kline.hpp"
#include "utils/ThreadPool.hpp"

namespace fileopt {

/**
 * @brief 历史K线数据加载器
 *
 * 单文件读取用于调试；批量读取用于生产环境。
 * 所有文件均按 JSONL 逐行解析，每行一个 Kline 对象。
 */
class DataLoader {
public:
#if CPPSTD_HAS_CPP17
    // 简化 std::filesystem 书写
    namespace fs = std::filesystem;
#endif

    /**
     * @brief 构造 DataLoader 并初始化内部线程池
     * @param numThreads 线程池大小，建议为 CPU 核心数的 1~2 倍
     */
    explicit DataLoader(size_t numThreads) : tasks_(numThreads) {}

    /**
     * @brief 加载单只股票的历史K线（C 字符串路径）
     * @param filepath 文件完整路径，如 "data/600000.jsonl"
     * @return 成功返回 K 线序列；失败返回 std::nullopt
     * @note 同步读取，不走线程池，适合调试与单元测试
     */
    std::optional<std::vector<config::Kline>> LoadKlines(const char* filepath) {
        return LoadFromFileImpl(filepath);
    }

#if CPPSTD_HAS_CPP17
    /**
     * @brief 加载单只股票的历史K线（fs::path 路径）
     * @param filepath std::filesystem 路径对象
     * @return 成功返回 K 线序列；失败返回 std::nullopt
     */
    std::optional<std::vector<config::Kline>> LoadKlines(const fs::path& filepath) {
        return LoadFromFileImpl(filepath.string());
    }
#endif

    /**
     * @brief 并行批量加载多只股票的历史K线
     * @param symbols 股票代码列表，如 {"600000", "000001", "300750"}
     * @return key 为股票代码字符串，value 为该股票的K线序列
     * @note 文件路径约定为 "data/{symbol}.jsonl"
     */
    std::map<std::string, std::vector<config::Kline>> BatchLoad(
        const std::vector<std::string>& symbols
    ) {
        std::mutex resultMutex;
        std::condition_variable cv;
        std::map<std::string, std::vector<config::Kline>> result;
        size_t pending = symbols.size();

        for (const auto& symbol : symbols) {
            std::string path = "data/" + symbol + ".jsonl";

            tasks_.Enqueue([this, symbol, path, &resultMutex, &result, &cv, &pending]() {
                auto klines = LoadOne(symbol, path);

                {
                    std::lock_guard<std::mutex> lock(resultMutex);
                    if (klines) {
                        result[symbol] = std::move(*klines);
                    }
                    --pending;
                }

                cv.notify_one();
            });
        }

        std::unique_lock<std::mutex> lock(resultMutex);
        cv.wait(lock, [&pending] { return pending == 0; });

        return result;
    }

private:
    // 校验每根K线的 code 字段是否与 symbol 一致，防止文件内容与文件名不符
    std::optional<std::vector<config::Kline>> LoadOne(
        const std::string& symbol,
        const std::string& filepath
    ) {
        auto klines = LoadFromFileImpl(filepath);
        if (klines) {
            for (const auto& k : *klines) {
                if (std::to_string(k.code) != symbol) {
                    std::cerr << "[DataLoader] symbol mismatch: "
                              << symbol << " vs " << k.code << "\n";
                    return std::nullopt;
                }
            }
        }
        return klines;
    }

    // 所有加载路径最终汇聚于此：打开文件，逐行读 JSONL，解析成 Kline
    std::optional<std::vector<config::Kline>> LoadFromFileImpl(
        const std::string& filepath
    ) {
        FileGuard file;
        FILE* fp = file.Open(filepath.c_str(), "rb");
        if (!fp) {
            std::cerr << "[DataLoader] can't open file: " << filepath << "\n";
            return std::nullopt;
        }

        std::vector<config::Kline> klines;
        std::string line;

        while (!(line = file.ReadLine()).empty()) {
            auto k = ParseKlineLine(line);
            if (k) {
                klines.push_back(std::move(*k));
            }
        }

        if (klines.empty()) {
            std::cerr << "[DataLoader] no valid Kline data in: " << filepath << "\n";
            return std::nullopt;
        }

        return klines;
    }

    // 解析单行 JSONL 为 Kline；JSON 字段顺序固定，无嵌套结构
    static std::optional<config::Kline> ParseKlineLine(const std::string& line) {
        config::Kline k;
        if (!ExtractUInt32(line, "\"code\":", k.code))     return std::nullopt;
        if (!ExtractInt32(line, "\"ymd\":", k.ymd))        return std::nullopt;
        if (!ExtractDouble(line, "\"open\":", k.open))     return std::nullopt;
        if (!ExtractDouble(line, "\"high\":", k.high))     return std::nullopt;
        if (!ExtractDouble(line, "\"low\":", k.low))       return std::nullopt;
        if (!ExtractDouble(line, "\"close\":", k.close))   return std::nullopt;
        if (!ExtractInt64(line, "\"volume\":", k.volume))  return std::nullopt;
        return k;
    }

    // 从 JSON 字符串中按 key 提取 double
    static bool ExtractDouble(const std::string& s, const std::string& key, double& out) {
        size_t pos = s.find(key);
        if (pos == std::string::npos) return false;
        pos += key.size();
        try { out = std::stod(s.substr(pos)); } catch (...) { return false; }
        return true;
    }

    // 从 JSON 字符串中按 key 提取 uint32_t
    static bool ExtractUInt32(const std::string& s, const std::string& key, uint32_t& out) {
        size_t pos = s.find(key);
        if (pos == std::string::npos) return false;
        pos += key.size();
        try { out = static_cast<uint32_t>(std::stoul(s.substr(pos))); } catch (...) { return false; }
        return true;
    }

    // 从 JSON 字符串中按 key 提取 int32_t
    static bool ExtractInt32(const std::string& s, const std::string& key, int32_t& out) {
        size_t pos = s.find(key);
        if (pos == std::string::npos) return false;
        pos += key.size();
        try { out = std::stoi(s.substr(pos)); } catch (...) { return false; }
        return true;
    }

    // 从 JSON 字符串中按 key 提取 int64_t
    static bool ExtractInt64(const std::string& s, const std::string& key, int64_t& out) {
        size_t pos = s.find(key);
        if (pos == std::string::npos) return false;
        pos += key.size();
        try { out = std::stoll(s.substr(pos)); } catch (...) { return false; }
        return true;
    }

private:
    utils::ThreadPool tasks_;  // 并行加载线程池
};

} // namespace fileopt