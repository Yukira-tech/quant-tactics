# FEATURES.md — StrategyEngine 功能特性清单

## 项目定位

StrategyEngine 是一个双均线 + CTA 混合策略引擎，面向 A 股多标的行情，采用 C++ / Python / Go 三语言协作，通过运行时文件系统解耦通信。

系统采用三层智能架构：

```
C++ 规则层（速度）
    ↓ 失效自动降级
Transformer 模型层（模式识别）
    ↓ 失效自动降级
LLM Agent 层（开放推理）
    ↓ 失效自动降级
规则模板（确定性兜底）
```

每层失效自动降级，任意一层故障不拖垮整体。

---

## 一、混合语言架构

### 1.1 设计动机

传统多语言协作通常依赖 FFI、cgo、RPC 或共享内存，存在以下问题：

- 一个语言挂掉会连累其他语言
- 接口绑定复杂，调试困难
- 语言升级互相阻塞

StrategyEngine 采用**运行时文件通信**：各语言模块独立运行，通过读写约定格式的 JSON/JSONL 文件交换数据。

### 1.2 核心优势

| 优势 | 说明 |
|------|------|
| **进程隔离** | 一个模块崩溃不影响其他模块 |
| **独立部署** | 各语言可单独启动、停止、升级 |
| **天然异步** | 文件系统本身就是消息队列 |
| **可调试** | 任何中间状态都可以直接打开文件查看 |
| **零 FFI 依赖** | 不需要 cgo、extern、共享内存 |
| **契约清晰** | `bridge.hpp` 是字段名的唯一事实来源 |

### 1.3 通信机制

```
runtime_files/
├── factor_outputs/       C++ 因子计算结果
├── signals/              C++ 策略信号
├── positions/            C++ 持仓状态
├── portfolio/            C++ 组合快照
├── agent_decisions/      YukiPilot 决策输出
└── klines/               K线历史数据（JSONL）
```

所有写入采用**临时文件 + rename 原子写**，保证读方永远看不到半截数据。

### 1.4 容错策略

| 场景 | 处理 |
|------|------|
| 文件不存在 | 静默返回空，等待下一轮 |
| JSON 损坏 | 中文警告，跳过该文件 |
| 单行损坏（JSONL） | 跳过该行，继续读其他行 |
| 单标的失败 | 不影响其他标的 |

任何单点故障都不允许中断主循环。

### 1.5 契约管理

`backend/netService/bridge.hpp` 集中定义所有 JSON 字段名常量：

- 通用字段：`symbol`、`timestamp`
- K 线字段：`code`、`ymd`、`open`、`high`、`low`、`close`、`volume`
- 因子字段：`ma_short`、`ma_long`、`donchian_high`、`donchian_low`、`atr`
- 信号字段：`signal`、`strength`、`strategy_source`
- 持仓字段：`quantity`、`avg_cost`、`current_price`、`floating_pnl` 等
- 组合字段：`total_equity`、`max_drawdown`、`target_portfolio` 等
- Agent 决策字段：`final_decision`、`confidence`、`reason`

三端结构体 tag 严格对齐，改动字段只改 `bridge.hpp` 一处。

---

## 二、YukiPilot 决策增强层

YukiPilot 原为 `calcAgent`，是 StrategyEngine 的 Python 决策增强层。

### 2.1 定位

```
C++ 负责速度：因子毫秒级计算、确定性规则信号
YukiPilot 负责深度：多因子融合、模式识别、开放推理
```

不参与高频因子计算，不参与交易执行，只做决策和解释。

### 2.2 三层智能架构

| 层 | 技术 | 职责 |
|----|------|------|
| 规则层 | C++ 双均线 + CTA | 确定性信号，速度优先 |
| 模型层 | FactorTransformer | 模式识别，输出概率 + 置信度 |
| Agent 层 | LLM tool-calling | 开放推理，决策复核 |

**降级链**：LLM 不可用 → 模型推理；模型不可信 → 规则兜底；规则故障 → 主循环跳过，不崩溃。

### 2.3 Transformer 模型

`attention/model.py` 中的 `FactorTransformer`：

- **参数量**：68,036（约 260 KB）
- **输入**：10 维因子序列，长度 `seq_len`（默认 32）
- **输出**：3 类 logits（hold/buy/sell）+ sigmoid 置信度
- **结构**：Linear 投影 → 正弦位置编码 → 2 层 pre-norm Transformer Encoder → 双头输出
- **训练**：CPU 秒级，无需 GPU

### 2.4 信号融合

`skill/tools.py` 中的 `fuse_decision()` 融合模型输出与 C++ 信号：

| 场景 | 行为 |
|------|------|
| 模型高置信 + C++ 同向 | 同向加成，置信度提升 |
| 模型高置信 + C++ 冲突 | 取权重高者，降权 0.7 |
| 模型低置信 + C++ 强信号 | C++ 信号兜底，置信度 = 强度 × 0.75 |
| 模型低置信 + C++ 弱信号 | 降级 hold |
| 模型观望 + C++ 有信号 | hold，C++ 信号仅作参考 |

### 2.5 代码级安全护栏

`validate_llm_decision()` 强制执行，LLM 无权更改：

| 规则 | 说明 |
|------|------|
| 风控一票否决 | 风险 high 时 LLM 说 buy 也强制 hold |
| 置信度封顶 | 最高 0.95，防止过度自信 |
| 非法决策回退 | 非 buy/sell/hold 用基线替换 |
| 审计痕迹 | LLM 反驳基线时 reason 追加「LLM反驳基线」 |
| 异常降级 | LLM 超时/乱说话/解析失败 → 返回 None → 规则模板 |

### 2.6 LLM Agent 复核

`langchain/chain.py` 实现手写 tool-calling 循环：

- 5 个工具：因子快照、信号一致性、风险检查、仓位约束、模型基线
- 最多 6 轮，30 秒超时
- 流式输出：最终决策逐 token 打字机打印
- 无 langchain / 无 API key 时自动降级规则模板

### 2.7 训练流水线

`train/` 目录实现完整训练链路：

```
K 线 JSONL → 特征矩阵（10 维因子，与 C++ 同口径）
→ 滑窗切分（seq_len 窗口，未来 N 日收益打标签）
→ 8:2 时序切分 → Adam → DecisionLoss
→ 最佳 checkpoint 保存
```

特征顺序固定：`open/high/low/close/log1p(volume)/ma_short/ma_long/donchian_high/donchian_low/atr`

损失函数 `DecisionLoss` = 加权交叉熵 + 置信度校准 MSE，让置信度头说真话。

### 2.8 流式能力

- **训练进度**：batch 级单行进度条，16 格、loss、速度、ETA，滑动平均越跑越准
- **LLM 决策**：逐 token 打字机输出，工具调用阶段打印 `[LLM] 调用工具: xxx`
- **扩展口**：`Trainer.train(X, y, on_batch_end=callback)` 预留前端推送接口

---

## 三、C++ 双入口

### 3.1 设计背景

C++ 层存在两种代码组织方式：

| 入口 | 说明 |
|------|------|
| `main_single.cpp` | 单文件版，所有类集中在一个文件内 |
| `main_modular.cpp` | 模块化版，链接 `config/fileopt/runtime/utils` 头文件 |

两者最终产物基本一致：读取 K 线，回放计算，写出运行时文件。

### 3.2 双入口对比

| 维度 | main_single.cpp | main_modular.cpp |
|------|-----------------|------------------|
| 编译依赖 | 零头文件链接，最简单 | 需 include 路径正确 |
| 可维护性 | 单文件较长 | 结构清晰，符合架构 |
| 当前状态 | 功能完整，推荐使用 | 基础回放，部分功能未移植 |
| 调试难度 | 低 | 中，可能有未修复 bug |
| 适用场景 | 快速验证、演示 | 后续重构基础 |

### 3.3 为什么保留两个入口

- `main_single.cpp` 保证**立即能跑**，不折腾链接
- `main_modular.cpp` 保留**架构完整性**，`.hpp` 文件不白写
- 两个入口不能同时参与编译，因为都定义了 `main` 函数

### 3.4 编译方式

```bash
# 单文件版（推荐）
cd backend/calcFactors
g++ -std=c++17 -O2 -Wall -o engine main_single.cpp
./engine

# 模块化版（实验性）
g++ -std=c++17 -O2 -Wall -I. -o engine_modular main_modular.cpp
```

### 3.5 后续重构路线

高考后把 `main_single.cpp` 中每个 `====` 分隔的类，逐个迁回对应 `.hpp` 文件，最终让 `main_modular.cpp` 成为唯一正式入口。

---

## 四、其他核心特性

### 4.1 C++ 高性能因子计算

- 双均线（5/20）、唐奇安通道（20）、ATR（14）滚动窗口
- Kline 结构体 64 字节缓存行对齐，`static_assert` 强制验证
- 内置线程池：移动语义、有锁队列、原子停止
- FileGuard：RAII 句柄封装，移动语义正确转移所有权

### 4.2 Go 实时服务

- HTTP API：快照、组合、信号、训练进度、思考状态
- WebSocket 每 2 秒推送，连接后立即推送首帧
- 轮询式文件监听，检测变化自动重建快照
- 静态托管前端，根路径重定向到入口

### 4.3 原生前端 SPA

- hash 路由四页切换：仪表盘、行情监控、训练中心、Agent 思考
- WebSocket 断线 1s/2s/4s/8s/16s 指数退避重连
- 连续 5 次失败降级 HTTP 轮询，连续 3 次失败置灰不清空
- Canvas 权益曲线 + 损失曲线，设备像素比适配
- 二次元贴纸风格，CSS 变量统一管理，支持 reduced-motion

---

## 五、功能清单速查

- [x] 跨语言运行时文件通信（C++/Python/Go）
- [x] bridge.hpp 契约单一事实来源
- [x] 双均线因子（C++）
- [x] CTA 因子（C++）
- [x] 金叉死叉信号（C++）
- [x] 唐奇安突破信号（C++）
- [x] 持仓管理，T+1（C++）
- [x] 组合优化，贪心算法（C++）
- [x] 并行数据加载（C++）
- [x] 单文件入口 main_single.cpp（C++）
- [x] 模块化入口 main_modular.cpp（C++）
- [x] Transformer 模型推理（YukiPilot）
- [x] 信号融合（YukiPilot）
- [x] 风险检查（YukiPilot）
- [x] 仓位约束（YukiPilot）
- [x] 模型训练（YukiPilot）
- [x] LLM tool-calling 循环（YukiPilot）
- [x] 代码级安全护栏（YukiPilot）
- [x] 流式训练进度（YukiPilot）
- [x] 流式 LLM 决策输出（YukiPilot）
- [x] 三层降级架构（全系统）
- [x] HTTP API（Go）
- [x] WebSocket（Go）
- [x] 文件监听（Go）
- [x] 静态托管（Go）
- [x] 四页 SPA 路由（前端）
- [x] WS 降级重连（前端）
- [x] 权益曲线（前端）
- [x] 损失曲线（前端）
- [x] Agent 思考时间线（前端）

---

## 六、已知限制

- 策略未经过严格历史回测，参数为初始默认值
- Transformer 训练数据量不足，真实预测能力待验证
- LLM Agent 层未在真实市场验证
- 成交模型简化：固定股数、假设价格、忽略 T+1 和涨跌停
- 文件通信延迟为秒级，不适用于高频场景
- Go 服务器无鉴权，仅适合本地开发环境
- 组合优化为贪心算法，后续可替换为均值方差模型
- `main_modular.cpp` 尚未完整移植高级功能

---

## 七、未来路线

- [ ] 接入真实 A 股数据源
- [ ] 补全 A 股交易规则（整手、T+1、印花税）
- [ ] 加入沪深 300 基准对比
- [ ] 用真实数据重训模型
- [ ] 完善 Go 服务鉴权
- [ ] 文件监听换 fsnotify
- [ ] 将 `main_single.cpp` 中的类迁移到 `.hpp`，使 `main_modular.cpp` 成为唯一正式入口
- [ ] 模型量化 / 导出 ONNX 加速推理

---

## 八、项目状态

技术骨架完整，三层智能架构可运行，三语言全链路可跑通。

**核心价值**：混合语言架构设计、跨语言运行时通信、降级容错、代码级护栏、C++ 双入口工程实践。

**不可直接用于实盘交易。** 策略未回测，模型未在真实数据上训练。