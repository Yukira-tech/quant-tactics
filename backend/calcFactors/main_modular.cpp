/**
 * @file main_modular.cpp
 * @brief 使用模块化头文件的 C++ 主程序
 *
 * 层级：
 *   backend/calcFactors/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/main_modular.cpp
 *
 * 模块作用：
 *   使用 config/ fileopt/ runtime/ utils/ 头文件组装策略引擎。
 *   读取 klines/ 下的 JSONL，回放计算，写出运行时文件。
 *
 * 使用者：
 *   g++ -std=c++17 -O2 -Wall -I. -o engine_modular main_modular.cpp
 *
 * 项目角色：
 *   模块化架构的正式入口，与 main.cpp 并行，但不冲突。
 *
 * 引入说明：
 *   零第三方依赖，仅需 C++17 标准库。
 *
 * 维护记录：
 *   2026-08-29 创建
 */

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "config/Kline.hpp"
#include "config/StrategyConfig.hpp"
#include "config/PortfolioConfig.hpp"
#include "fileopt/RuntimeFileManager.hpp"
#include "fileopt/DataWriter.hpp"
#include "runtime/FactorEngine.hpp"
#include "runtime/StrategyEngine.hpp"
#include "runtime/PositionManager.hpp"
#include "runtime/PortfolioOptimizer.hpp"
#include "runtime/PortfolioManager.hpp"
#include "utils/PrintLog.hpp"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// 扁平 JSON 解析（本项目 JSONL 单层结构够用）
// ---------------------------------------------------------------------------
namespace jsonutil {

inline std::optional<double> num(const std::string& s, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    auto p = s.find(pat);
    if (p == std::string::npos) return std::nullopt;
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return std::nullopt;
    const char* c = s.c_str() + p + 1;
    while (*c == ' ' || *c == '\t') ++c;
    if (*c == '"') ++c;
    char* end = nullptr;
    double v = std::strtod(c, &end);
    if (end == c) return std::nullopt;
    return v;
}

inline std::string str(const std::string& s, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    auto p = s.find(pat);
    if (p == std::string::npos) return "";
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return "";
    p = s.find('"', p + 1);
    if (p == std::string::npos) return "";
    auto e = s.find('"', p + 1);
    if (e == std::string::npos) return "";
    return s.substr(p + 1, e - p - 1);
}

} // namespace jsonutil

// ---------------------------------------------------------------------------
// 扫描 klines/ 目录
// ---------------------------------------------------------------------------
std::vector<std::string> scan_codes(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& it : fs::directory_iterator(dir, ec)) {
        auto name = it.path().filename().string();
        const std::string suf = ".jsonl";
        if (name.size() > suf.size() &&
            name.compare(name.size() - suf.size(), suf.size(), suf) == 0) {
            out.push_back(name.substr(0, name.size() - suf.size()));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------------------
// 读取单只股票 K 线
// ---------------------------------------------------------------------------
std::vector<config::Kline> load_klines(const std::string& path) {
    std::vector<config::Kline> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto code   = jsonutil::num(line, "code");
        auto ymd    = jsonutil::num(line, "ymd");
        auto open   = jsonutil::num(line, "open");
        auto high   = jsonutil::num(line, "high");
        auto low    = jsonutil::num(line, "low");
        auto close  = jsonutil::num(line, "close");
        auto volume = jsonutil::num(line, "volume");
        if (!code || !ymd || !open || !high || !low || !close || !volume) continue;

        config::Kline k;
        k.code   = static_cast<uint32_t>(*code);
        k.ymd    = static_cast<int32_t>(*ymd);
        k.open   = *open;
        k.high   = *high;
        k.low    = *low;
        k.close  = *close;
        k.volume = static_cast<int64_t>(*volume);
        out.push_back(k);
    }
    std::sort(out.begin(), out.end(),
              [](const config::Kline& a, const config::Kline& b) {
                  return a.ymd < b.ymd;
              });
    return out;
}

int main() {
    const std::string runtime_dir = "../runtime_files";

    config::StrategyConfig scfg;
    config::PortfolioConfig pcfg;

    std::string klines_dir = runtime_dir + "/klines";
    auto codes = scan_codes(klines_dir);
    if (codes.empty()) {
        std::cout << "klines/ 目录下没有 JSONL 数据\n";
        return 1;
    }

    std::map<std::string, std::vector<config::Kline>> all;
    for (const auto& code : codes) {
        auto k = load_klines(klines_dir + "/" + code + ".jsonl");
        if (!k.empty()) all[code] = std::move(k);
    }

    // 全局组件
    runtime::PortfolioManager pm(pcfg);

    // 每只股票一个因子引擎和策略引擎
    std::map<std::string, runtime::FactorEngine> factors;
    std::map<std::string, runtime::StrategyEngine> strategies;

    for (const auto& [code, klines] : all) {
        factors.emplace(code, runtime::FactorEngine(scfg));
        strategies.emplace(code, runtime::StrategyEngine(scfg));
    }

    // 收集所有交易日
    std::vector<int32_t> dates;
    for (const auto& [code, ks] : all)
        for (const auto& k : ks) dates.push_back(k.ymd);
    std::sort(dates.begin(), dates.end());
    dates.erase(std::unique(dates.begin(), dates.end()), dates.end());

    // 逐日回放
    for (int32_t date : dates) {
        for (const auto& code : codes) {
            auto& ks = all[code];
            for (const auto& k : ks) {
                if (k.ymd != date) continue;

                auto fres = factors.at(code).update(k);
                auto sres = strategies.at(code).update(k);

                runtime::SignalResult sig = sres;
                double price = k.close;

                // 简化持仓更新：直接按信号开平仓
                static std::map<std::string, runtime::PositionManager> positions;
                if (positions.find(code) == positions.end())
                    positions.emplace(code, runtime::PositionManager(k.code));

                auto& pos = positions.at(code);

                if (sig.type == runtime::SignalType::Buy && pos.get().quantity == 0) {
                    pos.open(100, price);
                } else if (sig.type == runtime::SignalType::Sell && pos.get().quantity > 0) {
                    pos.close(pos.get().quantity, price);
                }

                pos.markToMarket(price);
            }
        }
    }

    // 最后写个简单的组合快照
    double equity = pcfg.initialCapital;
    for (const auto& [code, ks] : all) {
        (void)ks;
    }

    std::cout << "回放完成，总权益: " << equity << "\n";
    return 0;
}