/**
 * @file main.cpp
 * @brief calcFactors C++ 核心主程序
 *
 * 层级：
 *   backend/calcFactors/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/main.cpp
 *
 * 模块作用：
 *   单文件可编译的 C++ 策略引擎入口。
 *   聚合了因子计算、策略信号、持仓管理、组合管理和文件写出，
 *   从 klines/ 读取历史K线，回放后把结果写入 runtime_files/。
 *
 * 使用者：
 *   cd backend/calcFactors && g++ -std=c++17 -O2 -Wall -o engine main.cpp
 *   ./engine                   # 全量回放，写完后退出
 *   ./engine --replay 500      # 每根K线间隔 500ms
 *   ./engine --loop            # 回放完后监听 klines/ 追加的新K线
 *
 * 项目角色：
 *   C++ 后端唯一可执行入口，驱动整条规则策略链路。
 *
 * 引入说明：
 *   零第三方依赖，仅需 C++17 标准库。
 *   JSON 字段名全部走 ../netService/bridge.hpp 常量。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-28 增加 Agent 决策回读与 --loop 模式
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#include "../netService/bridge.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ============================================================================
// 日志工具
// ============================================================================
namespace slog {

inline std::string now() {
    auto t = std::time(nullptr);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    return buf;
}

inline void info(const std::string& msg) {
    std::cout << "[" << now() << "] " << msg << std::endl;
}

} // namespace slog

// ============================================================================
// 扁平 JSON 解析（仅处理本项目单层 JSON，如需嵌套再换 nlohmann/json）
// ============================================================================
namespace mini_json {

// 取数值字段；容忍 {"code":600000} 和 {"code":"600000"} 两种写法
inline std::optional<double> get_number(const std::string& json, const char* key) {
    std::string pat = std::string("\"") + key + "\"";
    auto pos = json.find(pat);
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return std::nullopt;
    const char* p = json.c_str() + pos + 1;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '"') ++p;
    char* end = nullptr;
    double v = std::strtod(p, &end);
    if (end == p) return std::nullopt;
    return v;
}

// 取字符串字段；找不到返回空串
inline std::string get_string(const std::string& json, const char* key) {
    std::string pat = std::string("\"") + key + "\"";
    auto pos = json.find(pat);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

} // namespace mini_json

// ============================================================================
// K 线结构体，字段与 bridge.hpp / Python 端 Kline dataclass 严格一致
// ============================================================================
struct Kline {
    uint32_t code   = 0;
    int32_t  ymd    = 0;
    double   open   = 0.0;
    double   high   = 0.0;
    double   low    = 0.0;
    double   close  = 0.0;
    int64_t  volume = 0;
};

// ============================================================================
// 策略参数
// 注意：与 Python train/dataset.py 的 build_feature_matrix 默认参数必须一致
// ============================================================================
struct StrategyConfig {
    int    ma_short_win  = 5;
    int    ma_long_win   = 20;
    int    donchian_win  = 20;
    int    atr_win       = 14;
    double atr_stop_mult = 2.0;
};

// ============================================================================
// 组合参数
// ============================================================================
struct PortfolioConfig {
    double init_cash      = 1000000.0;
    double single_weight  = 0.20;
    int    max_positions  = 5;
    double fee_rate       = 0.0003;
    double slippage       = 0.001;
    int    lot_size       = 100;
};

// ============================================================================
// 运行时文件管理器：原子写 + 目录维护
// ============================================================================
class RuntimeFileManager {
public:
    explicit RuntimeFileManager(std::string runtime_dir)
        : dir_(std::move(runtime_dir)) {
        for (const char* sub : {bridge::DIR_FACTOR_OUTPUTS, bridge::DIR_SIGNALS,
                                bridge::DIR_POSITIONS, bridge::DIR_PORTFOLIO,
                                bridge::DIR_AGENT_DECISIONS, bridge::DIR_KLINES}) {
            std::error_code ec;
            fs::create_directories(fs::path(dir_) / sub, ec);
        }
    }

    const std::string& dir() const { return dir_; }

    std::string path(const char* subdir, const std::string& filename) const {
        return (fs::path(dir_) / subdir / filename).string();
    }

    // 原子写：先写 .tmp 再 rename
    bool write_atomic(const std::string& full_path, const std::string& content) const {
        std::string tmp = full_path + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            if (!out) return false;
            out << content;
            out.flush();
        }
        std::error_code ec;
        fs::rename(tmp, full_path, ec);
        return !ec;
    }

    // 读整个文件；不存在返回 nullopt
    std::optional<std::string> read_all(const std::string& full_path) const {
        std::ifstream in(full_path);
        if (!in) return std::nullopt;
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    void remove(const std::string& full_path) const {
        std::error_code ec;
        fs::remove(full_path, ec);
    }

private:
    std::string dir_;
};

// ============================================================================
// 数据加载器：扫描代码列表 + 解析 JSONL
// ============================================================================
class DataLoader {
public:
    // 扫描 klines/ 目录，返回所有股票代码
    static std::vector<std::string> scan_codes(const std::string& klines_dir) {
        std::vector<std::string> codes;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(klines_dir, ec)) {
            auto name = entry.path().filename().string();
            const std::string suffix = ".jsonl";
            if (name.size() > suffix.size() &&
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                codes.push_back(name.substr(0, name.size() - suffix.size()));
            }
        }
        std::sort(codes.begin(), codes.end());
        return codes;
    }

    // 解析一行 JSONL 为 Kline；字段缺失返回 nullopt
    static std::optional<Kline> parse_line(const std::string& line) {
        if (line.empty() || line == "\n") return std::nullopt;
        Kline k;
        auto code   = mini_json::get_number(line, bridge::FIELD_CODE);
        auto ymd    = mini_json::get_number(line, bridge::FIELD_YMD);
        auto open   = mini_json::get_number(line, bridge::FIELD_OPEN);
        auto high   = mini_json::get_number(line, bridge::FIELD_HIGH);
        auto low    = mini_json::get_number(line, bridge::FIELD_LOW);
        auto close  = mini_json::get_number(line, bridge::FIELD_CLOSE);
        auto volume = mini_json::get_number(line, bridge::FIELD_VOLUME);
        if (!code || !ymd || !open || !high || !low || !close || !volume) {
            return std::nullopt;
        }
        k.code   = static_cast<uint32_t>(*code);
        k.ymd    = static_cast<int32_t>(*ymd);
        k.open   = *open;
        k.high   = *high;
        k.low    = *low;
        k.close  = *close;
        k.volume = static_cast<int64_t>(*volume);
        return k;
    }

    // 加载单只股票全部K线，按 ymd 升序
    static std::vector<Kline> load(const std::string& path) {
        std::vector<Kline> out;
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            if (auto k = parse_line(line)) out.push_back(*k);
        }
        std::sort(out.begin(), out.end(),
                  [](const Kline& a, const Kline& b) { return a.ymd < b.ymd; });
        return out;
    }
};

// ============================================================================
// 因子计算引擎
// 因子口径与 Python train/dataset.py 逐点对齐：
//   数据不足窗口输出 0.0；ATR 用 TR 简单平均；首根 TR = high - low
// ============================================================================
struct FactorOutput {
    double ma_short      = 0.0;
    double ma_long       = 0.0;
    double donchian_high = 0.0;
    double donchian_low  = 0.0;
    double atr           = 0.0;
    bool   ready         = false;
};

class FactorEngine {
public:
    explicit FactorEngine(StrategyConfig cfg) : cfg_(cfg) {}

    // 喂入一根K线，返回最新因子
    FactorOutput on_bar(const Kline& k) {
        double tr = k.high - k.low;
        if (prev_close_ > 0.0) {
            tr = std::max({k.high - k.low,
                           std::fabs(k.high - prev_close_),
                           std::fabs(k.low - prev_close_)});
        }
        prev_close_ = k.close;

        push(closes_, k.close, cfg_.ma_long_win);
        push(highs_,  k.high,  cfg_.donchian_win);
        push(lows_,   k.low,   cfg_.donchian_win);
        push(trs_,    tr,      cfg_.atr_win);

        FactorOutput f;
        f.ma_short = (closes_.size() >= static_cast<size_t>(cfg_.ma_short_win))
                         ? tail_mean(closes_, cfg_.ma_short_win) : 0.0;
        f.ma_long  = (closes_.size() >= static_cast<size_t>(cfg_.ma_long_win))
                         ? tail_mean(closes_, cfg_.ma_long_win) : 0.0;
        if (highs_.size() >= static_cast<size_t>(cfg_.donchian_win)) {
            f.donchian_high = *std::max_element(highs_.begin(), highs_.end());
            f.donchian_low  = *std::min_element(lows_.begin(), lows_.end());
        }
        f.atr = (trs_.size() >= static_cast<size_t>(cfg_.atr_win))
                    ? tail_mean(trs_, cfg_.atr_win) : 0.0;
        f.ready = closes_.size() >= static_cast<size_t>(cfg_.ma_long_win);
        return f;
    }

private:
    static void push(std::deque<double>& q, double v, int cap) {
        q.push_back(v);
        if (q.size() > static_cast<size_t>(cap)) q.pop_front();
    }

    static double tail_mean(const std::deque<double>& q, int win) {
        double sum = 0.0;
        auto it = q.end() - win;
        for (; it != q.end(); ++it) sum += *it;
        return sum / win;
    }

    StrategyConfig cfg_;
    std::deque<double> closes_, highs_, lows_, trs_;
    double prev_close_ = 0.0;
};

// ============================================================================
// 策略信号引擎
// 优先级：ATR 止损 > 唐奇安突破 > 双均线交叉 > hold
// ============================================================================
struct SignalOutput {
    std::string signal  = bridge::SIGNAL_HOLD;
    double      strength = 0.0;
    std::string source   = "none";
};

class StrategyEngine {
public:
    // holding: 是否持仓；avg_cost: 持仓成本（未持仓传 0）
    SignalOutput on_bar(const Kline& k, const FactorOutput& cur,
                        bool holding, double avg_cost) {
        SignalOutput out;

        if (!cur.ready) return out;

        // ATR 止损
        if (holding && cur.atr > 0.0 && avg_cost > 0.0 &&
            k.close < avg_cost - cfg_atr_stop_mult_ * cur.atr) {
            out.signal   = bridge::SIGNAL_SELL;
            out.strength = 0.9;
            out.source   = "atr_stop";
            prev_ = cur;
            return out;
        }

        // 唐奇安突破（用上一根通道，避免当根自我突破）
        if (prev_.ready && prev_.donchian_high > 0.0) {
            if (k.close > prev_.donchian_high) {
                out.signal   = bridge::SIGNAL_BUY;
                out.strength = clamp(0.7 + (k.close - prev_.donchian_high) /
                                               std::max(k.close, 1e-9) * 10.0,
                                     0.7, 0.95);
                out.source   = "donchian";
                prev_ = cur;
                return out;
            }
            if (k.close < prev_.donchian_low) {
                out.signal   = bridge::SIGNAL_SELL;
                out.strength = clamp(0.7 + (prev_.donchian_low - k.close) /
                                               std::max(k.close, 1e-9) * 10.0,
                                     0.7, 0.95);
                out.source   = "donchian";
                prev_ = cur;
                return out;
            }
        }

        // 双均线交叉
        if (prev_.ready && prev_.ma_long > 0.0 && cur.ma_long > 0.0) {
            bool golden = prev_.ma_short <= prev_.ma_long && cur.ma_short > cur.ma_long;
            bool death  = prev_.ma_short >= prev_.ma_long && cur.ma_short < cur.ma_long;
            if (golden) {
                double sep = (cur.ma_short - cur.ma_long) / cur.ma_long;
                out.signal   = bridge::SIGNAL_BUY;
                out.strength = clamp(0.5 + sep * 50.0, 0.5, 0.9);
                out.source   = "ma_cross";
            } else if (death) {
                double sep = (cur.ma_long - cur.ma_short) / cur.ma_long;
                out.signal   = bridge::SIGNAL_SELL;
                out.strength = clamp(0.5 + sep * 50.0, 0.5, 0.9);
                out.source   = "ma_cross";
            }
        }

        prev_ = cur;
        return out;
    }

    void set_atr_stop_mult(double m) { cfg_atr_stop_mult_ = m; }

private:
    static double clamp(double v, double lo, double hi) {
        return std::max(lo, std::min(hi, v));
    }

    FactorOutput prev_;
    double cfg_atr_stop_mult_ = 2.0;
};

// ============================================================================
// 单票持仓管理（A股 T+1）
// 今日买入进入 frozen，下一交易日转入 available 可卖
// ============================================================================
struct Position {
    int64_t  quantity     = 0;
    int64_t  available    = 0;
    int64_t  frozen       = 0;
    double   avg_cost     = 0.0;
    double   current_price = 0.0;

    double floating_pnl() const {
        return (current_price - avg_cost) * static_cast<double>(quantity);
    }

    void on_new_day() {
        available += frozen;
        frozen = 0;
    }

    int64_t buy(int64_t qty, double price) {
        if (qty <= 0) return 0;
        double total_cost = avg_cost * static_cast<double>(quantity) +
                            price * static_cast<double>(qty);
        quantity += qty;
        frozen   += qty;
        avg_cost  = total_cost / static_cast<double>(quantity);
        current_price = price;
        return qty;
    }

    int64_t sell(int64_t qty, double price) {
        int64_t real = std::min(qty, available);
        if (real <= 0) return 0;
        quantity  -= real;
        available -= real;
        current_price = price;
        if (quantity == 0) avg_cost = 0.0;
        return real;
    }

    void mark(double price) { current_price = price; }
};

// ============================================================================
// Agent 决策回读
// Python YukiPilot 把决策写到 agent_decisions/{code}.json，
// 置信度达标的决策可覆盖 C++ 规则信号
// ============================================================================
struct AgentDecision {
    std::string final_decision;
    double      confidence = 0.0;
    std::string reason;
};

inline std::optional<AgentDecision> read_agent_decision(
        const RuntimeFileManager& files, const std::string& code) {
    auto content = files.read_all(
        files.path(bridge::DIR_AGENT_DECISIONS, code + ".json"));
    if (!content) return std::nullopt;
    AgentDecision d;
    d.final_decision = mini_json::get_string(*content, bridge::FIELD_FINAL_DECISION);
    d.confidence = mini_json::get_number(*content, bridge::FIELD_CONFIDENCE)
                       .value_or(0.0);
    d.reason = mini_json::get_string(*content, bridge::FIELD_REASON);
    if (d.final_decision.empty()) return std::nullopt;
    return d;
}

// ============================================================================
// 数据写出器：序列化输出，字段名全部走 bridge.hpp 常量
// ============================================================================
class DataWriter {
public:
    explicit DataWriter(const RuntimeFileManager& files) : files_(files) {}

    static std::string ymd_to_str(int32_t ymd) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                      ymd / 10000, (ymd / 100) % 100, ymd % 100);
        return buf;
    }

    void write_factor(const std::string& code, int32_t ymd,
                      const FactorOutput& f) const {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "{\"%s\":\"%s\",\"%s\":\"%s\",\"%s\":%.4f,\"%s\":%.4f,"
            "\"%s\":%.4f,\"%s\":%.4f,\"%s\":%.4f}",
            bridge::FIELD_SYMBOL, code.c_str(),
            bridge::FIELD_TIMESTAMP, ymd_to_str(ymd).c_str(),
            bridge::FIELD_MA_SHORT, f.ma_short,
            bridge::FIELD_MA_LONG, f.ma_long,
            bridge::FIELD_DONCHIAN_HIGH, f.donchian_high,
            bridge::FIELD_DONCHIAN_LOW, f.donchian_low,
            bridge::FIELD_ATR, f.atr);
        files_.write_atomic(files_.path(bridge::DIR_FACTOR_OUTPUTS, code + ".json"), buf);
    }

    void write_signal(const std::string& code, int32_t ymd,
                      const SignalOutput& s) const {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "{\"%s\":\"%s\",\"%s\":\"%s\",\"%s\":\"%s\",\"%s\":%.4f,\"%s\":\"%s\"}",
            bridge::FIELD_SYMBOL, code.c_str(),
            bridge::FIELD_TIMESTAMP, ymd_to_str(ymd).c_str(),
            bridge::FIELD_SIGNAL, s.signal.c_str(),
            bridge::FIELD_STRENGTH, s.strength,
            bridge::FIELD_STRATEGY_SOURCE, s.source.c_str());
        files_.write_atomic(files_.path(bridge::DIR_SIGNALS, code + ".json"), buf);
    }

    // 空仓则删文件，Go 端扫 positions/*.json 时就不会看到该标的
    void write_position(const std::string& code, int32_t ymd,
                        const Position& p) const {
        if (p.quantity == 0) {
            files_.remove(files_.path(bridge::DIR_POSITIONS, code + ".json"));
            return;
        }
        char buf[640];
        std::snprintf(buf, sizeof(buf),
            "{\"%s\":\"%s\",\"%s\":\"%s\",\"%s\":%lld,\"%s\":%.4f,"
            "\"%s\":%.4f,\"%s\":%.2f,\"%s\":%lld,\"%s\":%lld}",
            bridge::FIELD_SYMBOL, code.c_str(),
            bridge::FIELD_TIMESTAMP, ymd_to_str(ymd).c_str(),
            bridge::FIELD_QUANTITY, static_cast<long long>(p.quantity),
            bridge::FIELD_AVG_COST, p.avg_cost,
            bridge::FIELD_CURRENT_PRICE, p.current_price,
            bridge::FIELD_FLOATING_PNL, p.floating_pnl(),
            bridge::FIELD_INVENTORY_AVAILABLE, static_cast<long long>(p.available),
            bridge::FIELD_INVENTORY_FROZEN, static_cast<long long>(p.frozen));
        files_.write_atomic(files_.path(bridge::DIR_POSITIONS, code + ".json"), buf);
    }

    // portfolio/snapshot.json：Go 端固定读这个文件名
    void write_portfolio(double total_equity, double max_drawdown,
                         const std::vector<std::pair<std::string, int64_t>>& targets) const {
        std::ostringstream ss;
        ss << "{\"" << bridge::FIELD_TOTAL_EQUITY << "\":";
        char num[64];
        std::snprintf(num, sizeof(num), "%.2f", total_equity);
        ss << num << ",\"" << bridge::FIELD_MAX_DRAWDOWN << "\":";
        std::snprintf(num, sizeof(num), "%.4f", max_drawdown);
        ss << num << ",\"" << bridge::FIELD_TARGET_PORTFOLIO << "\":[";
        for (size_t i = 0; i < targets.size(); ++i) {
            if (i) ss << ",";
            ss << "{\"" << bridge::FIELD_SYMBOL << "\":\"" << targets[i].first
               << "\",\"" << bridge::FIELD_TARGET_QUANTITY << "\":"
               << targets[i].second << "}";
        }
        ss << "]}";
        files_.write_atomic(files_.path(bridge::DIR_PORTFOLIO, "snapshot.json"), ss.str());
    }

private:
    const RuntimeFileManager& files_;
};

// ============================================================================
// 全局组合管理器：资金、权益、回撤、目标组合
// ============================================================================
class PortfolioManager {
public:
    explicit PortfolioManager(PortfolioConfig cfg) : cfg_(cfg), cash_(cfg.init_cash) {}

    double cash() const { return cash_; }

    // 总权益 = 现金 + Σ持仓市值
    double total_equity(const std::map<std::string, Position>& positions) const {
        double eq = cash_;
        for (const auto& [code, p] : positions) {
            eq += static_cast<double>(p.quantity) * p.current_price;
        }
        return eq;
    }

    // 更新最大回撤，返回当前回撤（负值）
    double update_drawdown(double equity) {
        peak_ = std::max(peak_, equity);
        if (peak_ <= 0.0) return 0.0;
        double dd = equity / peak_ - 1.0;
        max_drawdown_ = std::min(max_drawdown_, dd);
        return max_drawdown_;
    }
    double max_drawdown() const { return max_drawdown_; }

    // 执行买入：按信号强度分配权重，整手取整，扣手续费+滑点
    int64_t execute_buy(Position& pos, double price, double strength,
                        double equity, int current_holdings) {
        if (pos.quantity == 0 && current_holdings >= cfg_.max_positions) return 0;
        double weight = cfg_.single_weight * std::min(strength, 1.0);
        double budget = std::min(cash_, equity * weight);
        double exec_price = price * (1.0 + cfg_.slippage);
        if (exec_price <= 0.0) return 0;
        int64_t qty = static_cast<int64_t>(budget / exec_price /
                                           cfg_.lot_size) * cfg_.lot_size;
        if (qty <= 0) return 0;
        double cost = qty * exec_price * (1.0 + cfg_.fee_rate);
        if (cost > cash_) return 0;
        cash_ -= cost;
        pos.buy(qty, exec_price);
        return qty;
    }

    // 执行卖出：全清可卖部分，扣手续费+滑点
    int64_t execute_sell(Position& pos, double price) {
        double exec_price = price * (1.0 - cfg_.slippage);
        int64_t qty = pos.sell(pos.available, exec_price);
        if (qty <= 0) return 0;
        cash_ += qty * exec_price * (1.0 - cfg_.fee_rate);
        return qty;
    }

    // 目标组合：按信号强度贪心排序，取前 max_positions 名
    std::vector<std::pair<std::string, int64_t>> build_target_portfolio(
            const std::map<std::string, SignalOutput>& signals,
            const std::map<std::string, Position>& positions,
            double equity) {
        std::vector<std::pair<std::string, double>> candidates;
        for (const auto& [code, sig] : signals) {
            bool holding = positions.count(code) && positions.at(code).quantity > 0;
            if (sig.signal == bridge::SIGNAL_BUY || holding) {
                candidates.emplace_back(code, sig.strength);
            }
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        std::vector<std::pair<std::string, int64_t>> targets;
        for (size_t i = 0; i < candidates.size() &&
                            static_cast<int>(i) < cfg_.max_positions; ++i) {
            const auto& code = candidates[i].first;
            double price = positions.count(code) ? positions.at(code).current_price : 0.0;
            if (price <= 0.0) continue;
            int64_t qty = static_cast<int64_t>(
                equity * cfg_.single_weight / price / cfg_.lot_size) * cfg_.lot_size;
            targets.emplace_back(code, qty);
        }
        return targets;
    }

private:
    PortfolioConfig cfg_;
    double cash_;
    double peak_         = 0.0;
    double max_drawdown_ = 0.0;
};

// ============================================================================
// 单票上下文：因子引擎 + 策略引擎 + 持仓 + 最新输出
// ============================================================================
struct StockContext {
    std::string     code;
    FactorEngine    factors;
    StrategyEngine  strategy;
    Position        position;
    FactorOutput    last_factor;
    SignalOutput    last_signal;
    size_t          next_bar = 0;

    explicit StockContext(std::string c, const StrategyConfig& cfg)
        : code(std::move(c)), factors(cfg) {
        strategy.set_atr_stop_mult(cfg.atr_stop_mult);
    }
};

// ============================================================================
// 主循环
// ============================================================================
namespace {

struct Options {
    std::string runtime_dir = "../runtime_files";
    int         replay_ms   = 0;
    bool        loop        = false;
    double      agent_min_conf = 0.6;
};

void print_usage(const char* prog) {
    std::cout <<
        "用法: " << prog << " [选项]\n"
        "  --runtime-dir <路径>   runtime_files 目录（默认 ../runtime_files）\n"
        "  --replay <毫秒>        回放模式：每根K线间隔指定毫秒\n"
        "  --loop                 回放完后继续监听 klines/ 追加的新K线\n"
        "  --agent-min-conf <值>  采纳 Agent 决策的最低置信度（默认 0.6）\n"
        "  -h, --help             显示帮助\n";
}

Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "参数 " << name << " 缺值\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--runtime-dir")      opt.runtime_dir = need_value("--runtime-dir");
        else if (a == "--replay")      opt.replay_ms = std::stoi(need_value("--replay"));
        else if (a == "--loop")        opt.loop = true;
        else if (a == "--agent-min-conf") opt.agent_min_conf = std::stod(need_value("--agent-min-conf"));
        else if (a == "-h" || a == "--help") { print_usage(argv[0]); std::exit(0); }
        else { std::cerr << "未知参数: " << a << "\n"; print_usage(argv[0]); std::exit(2); }
    }
    return opt;
}

// 处理一根K线：因子 → 信号 → 交易 → 写文件
void process_bar(StockContext& ctx, const Kline& k,
                 PortfolioManager& portfolio,
                 const DataWriter& writer,
                 const RuntimeFileManager& files,
                 const Options& opt,
                 int32_t prev_ymd,
                 std::map<std::string, SignalOutput>& all_signals,
                 std::map<std::string, Position>& all_positions) {
    // T+1：换交易日时解冻昨日买入
    if (k.ymd != prev_ymd) ctx.position.on_new_day();

    ctx.last_factor = ctx.factors.on_bar(k);
    writer.write_factor(ctx.code, k.ymd, ctx.last_factor);

    SignalOutput sig = ctx.strategy.on_bar(k, ctx.last_factor,
                                           ctx.position.quantity > 0,
                                           ctx.position.avg_cost);

    // Agent 决策覆盖：置信度达标才采纳
    if (auto dec = read_agent_decision(files, ctx.code)) {
        if (dec->confidence >= opt.agent_min_conf &&
            dec->final_decision != sig.signal) {
            slog::info("[" + ctx.code + "] Agent 覆盖信号: " + sig.signal +
                       " -> " + dec->final_decision +
                       " (conf=" + std::to_string(dec->confidence).substr(0, 4) +
                       ") " + dec->reason);
            sig.signal = dec->final_decision;
            sig.strength = std::max(sig.strength, dec->confidence);
            sig.source = "llm_agent";
        }
    }
    ctx.last_signal = sig;
    writer.write_signal(ctx.code, k.ymd, sig);

    // 交易执行
    std::map<std::string, Position> snapshot_positions = all_positions;
    snapshot_positions[ctx.code] = ctx.position;
    double equity = portfolio.total_equity(snapshot_positions);
    int holdings = 0;
    for (const auto& [c, p] : snapshot_positions) if (p.quantity > 0) ++holdings;

    if (sig.signal == bridge::SIGNAL_BUY || sig.signal == bridge::SIGNAL_COVER) {
        int64_t qty = portfolio.execute_buy(ctx.position, k.close, sig.strength,
                                            equity, holdings);
        if (qty > 0) {
            slog::info("[" + ctx.code + "] 买入 " + std::to_string(qty) +
                       " 股 @ " + DataWriter::ymd_to_str(k.ymd) +
                       " close=" + std::to_string(k.close).substr(0, 6) +
                       " (" + sig.source + ")");
        }
    } else if (sig.signal == bridge::SIGNAL_SELL || sig.signal == bridge::SIGNAL_SHORT) {
        int64_t qty = portfolio.execute_sell(ctx.position, k.close);
        if (qty > 0) {
            slog::info("[" + ctx.code + "] 卖出 " + std::to_string(qty) +
                       " 股 @ " + DataWriter::ymd_to_str(k.ymd) +
                       " close=" + std::to_string(k.close).substr(0, 6) +
                       " (" + sig.source + ")");
        }
    }

    // 盯市 + 写持仓
    ctx.position.mark(k.close);
    writer.write_position(ctx.code, k.ymd, ctx.position);

    all_signals[ctx.code] = sig;
    all_positions[ctx.code] = ctx.position;
}

// 写一次组合快照
void flush_portfolio(PortfolioManager& portfolio,
                     const DataWriter& writer,
                     const std::map<std::string, SignalOutput>& signals,
                     const std::map<std::string, Position>& positions) {
    double equity = portfolio.total_equity(positions);
    double max_dd = portfolio.update_drawdown(equity);
    auto targets = portfolio.build_target_portfolio(signals, positions, equity);
    writer.write_portfolio(equity, max_dd, targets);
}

} // namespace

int main(int argc, char** argv) {
    Options opt = parse_args(argc, argv);

    RuntimeFileManager files(opt.runtime_dir);
    DataWriter writer(files);
    StrategyConfig strategy_cfg;
    PortfolioManager portfolio(PortfolioConfig{});

    std::string klines_dir = files.path(bridge::DIR_KLINES, "");
    auto codes = DataLoader::scan_codes(klines_dir);
    if (codes.empty()) {
        slog::info("klines/ 目录下没有 .jsonl 数据文件，把K线数据放进去再跑");
        slog::info("格式示例: {\"code\":600000,\"ymd\":20240102,\"open\":10.1,"
                   "\"high\":10.3,\"low\":10.0,\"close\":10.2,\"volume\":123456}");
        return 1;
    }

    std::map<std::string, std::vector<Kline>> all_klines;
    std::map<std::string, StockContext> contexts;
    for (const auto& code : codes) {
        auto ks = DataLoader::load(files.path(bridge::DIR_KLINES, code + ".jsonl"));
        if (ks.empty()) {
            slog::info("[" + code + "] 文件为空或全部解析失败，跳过");
            continue;
        }
        slog::info("[" + code + "] 加载 " + std::to_string(ks.size()) + " 根K线 (" +
                   DataWriter::ymd_to_str(ks.front().ymd) + " ~ " +
                   DataWriter::ymd_to_str(ks.back().ymd) + ")");
        contexts.emplace(code, StockContext(code, strategy_cfg));
        all_klines.emplace(code, std::move(ks));
    }
    if (contexts.empty()) return 1;

    slog::info("开始回放...");
    std::map<std::string, SignalOutput> all_signals;
    std::map<std::string, Position>     all_positions;

    std::vector<int32_t> all_dates;
    for (const auto& [code, ks] : all_klines) {
        for (const auto& k : ks) all_dates.push_back(k.ymd);
    }
    std::sort(all_dates.begin(), all_dates.end());
    all_dates.erase(std::unique(all_dates.begin(), all_dates.end()), all_dates.end());

    for (int32_t date : all_dates) {
        for (auto& [code, ctx] : contexts) {
            auto& ks = all_klines[code];
            while (ctx.next_bar < ks.size() && ks[ctx.next_bar].ymd == date) {
                process_bar(ctx, ks[ctx.next_bar], portfolio, writer, files, opt,
                            ctx.next_bar > 0 ? ks[ctx.next_bar - 1].ymd : 0,
                            all_signals, all_positions);
                ++ctx.next_bar;
                if (opt.replay_ms > 0) {
                    flush_portfolio(portfolio, writer, all_signals, all_positions);
                    std::this_thread::sleep_for(std::chrono::milliseconds(opt.replay_ms));
                }
            }
        }
        if (opt.replay_ms == 0) flush_portfolio(portfolio, writer, all_signals, all_positions);
    }
    flush_portfolio(portfolio, writer, all_signals, all_positions);

    double equity = portfolio.total_equity(all_positions);
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "回放结束: 总权益 %.2f, 收益率 %+.2f%%, 最大回撤 %.2f%%",
                      equity, (equity / PortfolioConfig{}.init_cash - 1.0) * 100.0,
                      portfolio.max_drawdown() * 100.0);
        slog::info(buf);
    }

    if (opt.loop) {
        slog::info("进入实时监听模式（Ctrl+C 退出），盯着 klines/ 的新行...");
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            for (auto& [code, ctx] : contexts) {
                auto ks = DataLoader::load(
                    files.path(bridge::DIR_KLINES, code + ".jsonl"));
                bool got_new = false;
                while (ctx.next_bar < ks.size()) {
                    process_bar(ctx, ks[ctx.next_bar], portfolio, writer, files, opt,
                                ctx.next_bar > 0 ? ks[ctx.next_bar - 1].ymd : 0,
                                all_signals, all_positions);
                    ++ctx.next_bar;
                    got_new = true;
                }
                if (got_new) flush_portfolio(portfolio, writer, all_signals, all_positions);
            }
            // 新上市的股票也能被发现
            for (const auto& code : DataLoader::scan_codes(klines_dir)) {
                if (contexts.count(code)) continue;
                auto ks = DataLoader::load(files.path(bridge::DIR_KLINES, code + ".jsonl"));
                if (ks.empty()) continue;
                slog::info("发现新标的 [" + code + "]，加入计算");
                contexts.emplace(code, StockContext(code, strategy_cfg));
                all_klines.emplace(code, std::move(ks));
            }
        }
    }

    return 0;
}