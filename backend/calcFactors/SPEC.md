# C++ 层内部接口说明

维护者：Yukira  
适用：backend/calcFactors/  
跨语言字段契约：以 `docs/CONTRACT.md` 与 `bridge.hpp` 为准

---

## 1. 模块边界

| 模块 | 路径 | 职责 |
|------|------|------|
| `config` | `config/` | 数据结构与策略/组合配置 |
| `fileopt` | `fileopt/` | 文件读写与运行时文件管理 |
| `runtime` | `runtime/` | 因子计算、信号生成、持仓与组合管理 |
| `utils` | `utils/` | 线程池与日志工具 |

所有头文件按 `docs/COMMENT_STYLE.md` 写文件头注释，类与公共接口用 Doxygen 格式。

---

## 2. 编译入口

本项目 C++ 层存在两个入口文件，最终产物基本一致：

| 文件 | 说明 |
|------|------|
| `main_single.cpp` | 单文件版，所有逻辑集中在一个文件内，推荐优先使用 |
| `main_modular.cpp` | 模块化版，链接 `config/fileopt/runtime/utils` 头文件，实验性 |

两个文件不能同时参与编译，因为都定义了 `main` 函数。构建时只选一个入口。

---

## 3. 数据结构

### Kline

文件：`config/Kline.hpp`

| 字段 | 类型 | 说明 |
|------|------|------|
| `code` | uint32_t | 6 位股票代码 |
| `ymd` | int32_t | 年月日整数 |
| `open` | double | 开盘价 |
| `high` | double | 最高价 |
| `low` | double | 最低价 |
| `close` | double | 收盘价 |
| `volume` | int64_t | 成交量 |

结构体 64 字节缓存行对齐，`static_assert` 强制验证。

排序语义：先按 code，再按 ymd。

### FactorResult

文件：`runtime/FactorEngine.hpp`

| 字段 | 类型 | 说明 |
|------|------|------|
| `maShort` | double | 短期均线 |
| `maLong` | double | 长期均线 |
| `donchianHigh` | double | 唐奇安上轨 |
| `donchianLow` | double | 唐奇安下轨 |
| `atr` | double | ATR |
| `close` | double | 当前收盘价 |

### SignalResult

文件：`runtime/StrategyEngine.hpp`

| 字段 | 类型 | 说明 |
|------|------|------|
| `type` | SignalType | 枚举：Hold / Buy / Sell / Short / Cover |
| `strength` | double | 信号强度 0~1 |
| `strategySource` | string | 信号来源 |

### Position

文件：`runtime/PositionManager.hpp`

| 字段 | 类型 | 说明 |
|------|------|------|
| `code` | uint32_t | 股票代码 |
| `quantity` | int64_t | 持仓数量 |
| `avgCost` | double | 平均成本 |
| `currentPrice` | double | 最新价 |
| `floatingPnl` | double | 浮动盈亏 |
| `inventoryAvailable` | int64_t | 可用库存 |
| `inventoryFrozen` | int64_t | 冻结库存 |

---

## 4. 核心接口

### FactorEngine

文件：`runtime/FactorEngine.hpp`

```cpp
explicit FactorEngine(const config::StrategyConfig& cfg);
FactorResult update(const config::Kline& k);
void reset();
```

`update()` 输入单根 K 线，返回该根对应的全部因子。

因子口径：

- SMA：窗口不足时返回 0
- 唐奇安通道：窗口不足时上下轨为 0
- ATR：TR 的简单移动平均，首根 TR 用 high - low

该口径必须与 Python 侧 `train/dataset.py` 的 `build_feature_matrix` 完全一致。

### StrategyEngine

文件：`runtime/StrategyEngine.hpp`

```cpp
explicit StrategyEngine(const config::StrategyConfig& cfg);
SignalResult update(const config::Kline& k);
void reset();
```

信号优先级：

1. CTA 唐奇安突破
2. 双均线金叉死叉
3. 默认 CTA > 双均线，可通过 `StrategyConfig::ctaPriority` 调整

### PositionManager

文件：`runtime/PositionManager.hpp`

```cpp
explicit PositionManager(uint32_t code);
void open(int64_t qty, double price);
void close(int64_t qty, double price);
void markToMarket(double price);
const Position& get() const;
```

`close()` 卖出数量超过持仓时，只卖出当前持仓。

### PortfolioOptimizer

文件：`runtime/PortfolioOptimizer.hpp`

```cpp
explicit PortfolioOptimizer(const config::PortfolioConfig& cfg);
std::map<uint32_t, int64_t> optimize(
    const std::map<uint32_t, SignalResult>& signals,
    double totalEquity);
```

MVP 阶段使用贪心策略：按信号强度排序，依次分配仓位。假设股价 10 元，后续替换为真实价格。

### PortfolioManager

文件：`runtime/PortfolioManager.hpp`

```cpp
explicit PortfolioManager(const config::PortfolioConfig& cfg);
void updatePosition(uint32_t code, const SignalResult& sig, double price);
double calcTotalEquity() const;
std::map<uint32_t, int64_t> optimize();
```

---

## 5. 数据流

```
klines/{code}.jsonl
  → DataLoader 读取
  → FactorEngine 计算因子
  → StrategyEngine 生成信号
  → PositionManager 更新持仓
  → PortfolioOptimizer 计算目标组合
  → PortfolioManager 汇总
  → DataWriter 原子写入 factor_outputs/ signals/ positions/ portfolio/
```

Agent 决策回读：

```
agent_decisions/{code}.json → 置信度达标 → 覆盖 C++ 规则信号
```

---

## 6. 错误处理约定

- 文件不存在或 JSON 解析失败：跳过，返回空或 nullopt
- 单只股票处理失败：警告，不影响其他标的
- 因子窗口不足：返回 0 或 hold，不产生交易信号
- 原子写失败：警告，不中断主循环

任何情况下主循环不得崩溃。