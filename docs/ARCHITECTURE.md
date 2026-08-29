# StrategyEngine 架构文档

维护者：Yukira  
更新日期：2026-08-29  
状态：技术骨架完成，策略未回测，不可用于实盘

---

## 1. 项目定位

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

每一层失效都会自动降级到下一层，任意一层故障不会拖垮整体。

---

## 2. 整体架构图

```
┌─────────────────────────────────────────────────────────────┐
│                        Frontend (浏览器)                     │
│                   HTML / CSS / JavaScript                    │
│               hash 路由 + WebSocket + HTTP 轮询降级           │
└──────────────────────────┬──────────────────────────────────┘
                           │ WebSocket / HTTP
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                    netService / goServer                     │
│                                                             │
│  main.go      HTTP 路由 + WebSocket + 静态托管               │
│  snapshot.go  从运行时文件构建快照，缓存 + 排序               │
│  watcher.go   轮询监控 runtime_files 目录变化                 │
└──────────────────────────┬──────────────────────────────────┘
                           │ 读取
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                     runtime_files /                         │
│                                                             │
│  factor_outputs/    C++ 因子计算结果                         │
│  signals/           C++ 策略信号                             │
│  positions/         C++ 持仓状态                             │
│  portfolio/         组合快照                                 │
│  agent_decisions/   YukiPilot 决策输出                       │
│  klines/            历史K线（JSONL）                          │
└──────────┬──────────────────────────────┬───────────────────┘
           │ 写入                         │ 读取/写入
           ▼                              ▼
┌──────────────────────────┐   ┌──────────────────────────────┐
│  calcFactors (C++ 核心)   │   │      YukiPilot (Python)      │
│                          │   │                              │
│ config/   配置与数据结构  │   │ 读取因子/信号/持仓/K线        │
│ fileopt/  文件IO与加载    │   │ 模型推理 + 规则融合           │
│ runtime/  因子/策略/持仓  │   │ LLM Agent 复核 + 代码护栏     │
│ utils/    线程池/日志     │   │ 写回 agent_decisions/        │
│ main_single.cpp          │   │                              │
│ main_modular.cpp         │   │                              │
└──────────────────────────┘   └──────────────────────────────┘
```

---

## 3. 目录结构

```
StrategyEngine/
├─ backend/
│  ├─ calcFactors/               # C++ 因子计算与规则策略
│  │  ├─ config/
│  │  │  ├─ Kline.hpp
│  │  │  ├─ PortfolioConfig.hpp
│  │  │  ├─ StrategyConfig.hpp
│  │  │  └─ VersionConfig.hpp
│  │  ├─ fileopt/
│  │  │  ├─ DataLoader.hpp
│  │  │  ├─ DataWriter.hpp
│  │  │  ├─ FileGuard.hpp
│  │  │  ├─ FileSystem.hpp
│  │  │  └─ RuntimeFileManager.hpp
│  │  ├─ runtime/
│  │  │  ├─ Clock.hpp
│  │  │  ├─ FactorEngine.hpp
│  │  │  ├─ PortfolioManager.hpp
│  │  │  ├─ PortfolioOptimizer.hpp
│  │  │  ├─ PositionManager.hpp
│  │  │  └─ StrategyEngine.hpp
│  │  ├─ utils/
│  │  │  ├─ PrintLog.hpp
│  │  │  └─ ThreadPool.hpp
│  │  ├─ main_single.cpp         # 单文件版，推荐使用
│  │  ├─ main_modular.cpp        # 模块化版，实验性
|  |  └─ SPEC.md
│  │
│  ├─ YukiPilot/                 # Python 决策增强层（原 calcAgent）
│  │  ├─ attention/              # Transformer 模型
│  │  ├─ config/                 # 配置类
│  │  ├─ dataclass/              # 数据契约 + 运行时文件 IO
│  │  ├─ langchain/              # LLM Agent 决策复核
│  │  ├─ loss/                   # 决策损失函数
│  │  ├─ skill/                  # 决策技能纯函数
│  │  ├─ train/                  # 数据集构建与训练
│  │  ├─ checkpoints/            # 模型权重
│  │  ├─ main.py                 # 主循环入口
│  │  ├─ requirements.txt
│  │  ├─ SPEC.md
│  │  └─ TRAINING.md
│  │
│  ├─ netService/
│  │  ├─ goServer/
│  │  │  ├─ main.go
│  │  │  ├─ snapshot.go
│  │  │  ├─ watcher.go
│  │  │  ├─ go.mod
│  │  │  └─ SPEC.md
│  │  └─ bridge.hpp              # 跨语言 JSON 契约
│  │
│  └─ runtime_files/             # 运行时共享数据目录
│     ├─ agent_decisions/
│     ├─ factor_outputs/
│     ├─ klines/
│     ├─ portfolio/
│     ├─ positions/
│     └─ signals/
│
├─ frontend/                     # 前端 SPA 监控面板
│  ├─ public/
│  │  └─ index.html
│  └─ src/
│     ├─ api/
│     │  ├─ bus.js
│     │  ├─ endpoints.js
│     │  ├─ http.js
│     │  └─ ws.js
│     ├─ dom/
│     │  ├─ page_agent.js
│     │  ├─ page_dashboard.js
│     │  ├─ page_monitor.js
│     │  └─ page_training.js
│     ├─ image/
│     │  └─ *.svg
│     ├─ style/
│     │  ├─ base.css
│     │  ├─ components.css
│     │  └─ tokens.css
│     ├─ templates/
│     │  ├─ epoch-row.html
│     │  ├─ position-row.html
│     │  ├─ signal-row.html
│     │  ├─ target-item.html
│     │  └─ think-card.html
│     ├─ app.js
│     ├─ router.js
|     └─ SPEC.md
│
└─ docs/
   ├─ README.md
   ├─ ARCHITECTURE.md            # 本文档
   ├─ CONTRACT.md                # 跨语言总契约
   ├─ FEATURES.md
   ├─ TIMING.md
   ├─ COMMENT_STYLE.md
   └─ CHANGELOG.md
```

---

## 4. C++ 层：calcFactors

### 定位

负责高性能因子计算和规则信号生成。不参与模型推理，不参与 LLM 决策。

### 编译入口

C++ 层存在两个入口文件，最终产物基本一致：

| 文件 | 说明 |
|------|------|
| `main_single.cpp` | 单文件版，所有逻辑集中在一个文件内，推荐优先使用 |
| `main_modular.cpp` | 模块化版，链接 `config/fileopt/runtime/utils` 头文件，实验性 |

两个文件不能同时参与编译，因为都定义了 `main` 函数。详细说明见 `backend/calcFactors/SPEC.md`。

### 核心模块

| 模块 | 路径 | 职责 |
|------|------|------|
| `config` | `config/` | 数据结构与策略/组合配置 |
| `fileopt` | `fileopt/` | 文件 IO 与运行时文件管理 |
| `runtime` | `runtime/` | 因子计算、信号生成、持仓与组合管理 |
| `utils` | `utils/` | 线程池与日志工具 |

### 数据流

```
klines/{code}.jsonl
  → DataLoader 读取
  → FactorEngine 计算因子
  → StrategyEngine 生成信号
  → read_agent_decision() 检查 YukiPilot 决策覆盖
  → PositionManager 更新持仓（T+1）
  → PortfolioManager 汇总
  → DataWriter 原子写出运行时文件
```

### 关键接口

- `FactorEngine::update(Kline) -> FactorResult`
- `StrategyEngine::update(Kline) -> SignalResult`
- `PositionManager::open/close/markToMarket`
- `PortfolioOptimizer::optimize(signals, equity) -> targets`
- `PortfolioManager::updatePosition/calcTotalEquity/optimize`

详细契约见 `backend/calcFactors/SPEC.md`。

---

## 5. Python 层：YukiPilot

### 定位

策略引擎的决策增强层，不参与高频因子计算。负责多因子融合、模型推理、LLM 复核和风控降级。

### 核心能力

| 能力 | 说明 |
|------|------|
| 模型推理 | 68K 参数 FactorTransformer，输入 10 维因子序列，输出三分类概率 + 置信度 |
| 信号融合 | `fuse_decision()` 融合模型输出与 C++ 信号 |
| 风控降级 | 浮亏超线强制 hold，持仓数达上限禁止买入 |
| LLM 复核 | 手写 tool-calling 循环，最多 6 轮，30 秒超时 |
| 代码护栏 | `validate_llm_decision()` 执行风控一票否决、置信度封顶、非法值回退 |
| 训练流水线 | JSONL → 特征矩阵 → 滑窗切分 → 训练 → checkpoint |
| 流式输出 | 训练进度、LLM 决策逐 token 打印 |

### 决策流程

```
读取 K 线 → 构建特征 → 模型推理 → fuse_decision
→ 风险/仓位检查 → LLM Agent 复核（可选）
→ validate_llm_decision 护栏 → 原子写回 agent_decisions/
```

### 运行方式

```bash
cd backend
python -m YukiPilot.main --once
python -m YukiPilot.main --train
python -m YukiPilot.main
```

详细接口见 `backend/YukiPilot/SPEC.md`，训练细节见 `backend/YukiPilot/TRAINING.md`。

---

## 6. Go 层：netService

### 定位

负责把运行时文件翻译成前端可用的 JSON，通过 HTTP 和 WebSocket 提供实时数据。

### 核心模块

| 文件 | 职责 |
|------|------|
| `main.go` | 路由注册、HTTP 处理器、WebSocket、静态托管 |
| `snapshot.go` | 快照管理器、数据契约、读取运行时文件 |
| `watcher.go` | 运行时文件变化监听 |

### HTTP 端点

| 端点 | 方法 | 说明 |
|------|------|------|
| `/api/snapshot` | GET | 完整快照 |
| `/api/portfolio` | GET | 组合信息 |
| `/api/signals` | GET | 全部信号 |
| `/api/training` | GET | 训练进度 |
| `/api/thinking` | GET | 全部思考状态 |
| `/ws` | WebSocket | 每 2 秒推送一次快照 |
| `/` | GET | 重定向到前端入口 |

详细接口见 `backend/netService/goServer/SPEC.md`。

---

## 7. 前端：frontend

### 定位

无框架原生 SPA，以二次元风格实时展示策略引擎状态。

### 技术要点

- hash 路由四页切换：仪表盘、行情监控、训练中心、Agent 思考
- WebSocket 实时推送，断开后指数退避重连，降级 HTTP 轮询
- Canvas 绘制权益曲线和损失曲线，设备像素比适配
- 全部样式通过 CSS 变量统一管理，支持 reduced-motion 无障碍降级
- 行模板独立存放，与 index.html 内联副本逐字节一致

### 脚本加载顺序

```
bus → endpoints → http → ws → router → 四个页面模块 → app
```

详细契约见 `frontend/SPEC.md`。

---

## 8. 跨语言通信：runtime_files

### 设计原则

各语言模块独立运行，通过读写约定格式的运行时文件协同。文件系统天然提供异步通信、持久化、解耦能力。

### 目录说明

```
runtime_files/
├── factor_outputs/       C++ FactorEngine 输出因子
├── signals/              C++ StrategyEngine 输出信号
├── positions/            C++ PositionManager 输出持仓
├── portfolio/            C++ PortfolioManager 输出组合快照
├── agent_decisions/      YukiPilot 输出最终决策
└── klines/               K线历史数据（JSONL）
```

### 原子写保证

所有实时输出文件采用临时文件 + rename 的原子写方式，保证读方永远看不到半截数据。

### 容错策略

| 场景 | 处理 |
|------|------|
| 文件不存在 | 静默返回空，等待下一轮 |
| JSON 损坏 | 打印中文警告，跳过该文件 |
| 单行损坏（JSONL） | 跳过该行，继续读其他行 |
| 单标的失败 | 不影响其他标的处理 |

### 字段契约

所有 JSON 字段名由 `bridge.hpp` 集中定义，C++ / Python / Go 三端严格对齐。改动字段必须先改 `bridge.hpp`，再同步更新三端结构体与 `docs/CONTRACT.md`。

---

## 9. 数据流

```
历史K线 JSONL
      ↓
C++ DataLoader 加载
      ↓
C++ FactorEngine 计算因子
      ↓
C++ StrategyEngine 生成信号
      ↓
检查 YukiPilot 决策覆盖
      ↓
C++ PositionManager 更新持仓（T+1）
      ↓
C++ DataWriter 原子写 factor_outputs/ signals/ positions/ portfolio/
      ↓
YukiPilot 读取文件 → 模型推理 → 规则融合 → LLM 复核 → 写回 agent_decisions/
      ↓
Go watcher 检测文件变化 → 重建快照
      ↓
Go 提供 HTTP API 与 WebSocket 推送
      ↓
前端渲染四页监控面板
```

---

## 10. 配置约定

### 策略参数

因子窗口参数在 C++ 与 Python 两侧必须保持一致：

| 参数 | 默认值 |
|------|--------|
| 短均线窗口 | 5 |
| 长均线窗口 | 20 |
| 唐奇安窗口 | 20 |
| ATR 窗口 | 14 |
| ATR 止损倍数 | 2.0 |

C++ 侧在 `StrategyConfig.hpp`，Python 侧在 `build_feature_matrix()` 默认参数。

### 信号枚举

```
buy | sell | hold | short | cover
```

### 决策枚举

```
buy | sell | hold
```

---

## 11. 错误处理约定

- C++：任何单标的处理失败不中断主循环，原子写失败警告不崩溃
- Python：文件缺失或 JSON 损坏返回 None 或空列表，LLM 异常降级规则模板，checkpoint 缺失随机初始化兜底
- Go：单个文件读取失败跳过，快照读取使用 RWMutex 保护，任何文件损坏不让服务器崩溃
- 前端：WS 断开自动重连，HTTP 失败保留旧数据，字段缺失显示 `--` 或空态，不允许白屏

---

## 12. 当前状态与限制

### 已完成

- 三语言协作全链路可跑通
- C++ 因子计算、规则信号、持仓管理、组合优化
- Python 模型推理、信号融合、LLM Agent 复核、代码护栏
- Go HTTP API、WebSocket、文件监听、静态托管
- 前端四页 SPA，实时监控面板
- 训练进度与思考过程可视化
- 完整文档体系

### 已知限制

- 策略未经过严格历史回测，参数为初始默认值
- 模型训练数据不足，真实预测能力待验证
- 成交模型简化：固定股数、假设价格、忽略 T+1 和涨跌停
- 文件通信延迟为秒级，不适用于高频场景
- Go 服务器无鉴权，仅适合本地开发环境
- 组合优化为贪心算法，后续可替换为均值方差模型
- `main_modular.cpp` 尚未完整移植高级功能

### 后续路线

1. 接入真实 A 股数据源
2. 补全 A 股交易规则
3. 加入沪深 300 基准对比
4. 用真实数据重训模型
5. 完善 Go 服务鉴权
6. 文件监听换 fsnotify
7. 把 `main_single.cpp` 中的类逐步迁移到 `.hpp`，让 `main_modular.cpp` 成为唯一正式入口

---

## 13. 维护注意事项

- 改跨语言字段：先改 `bridge.hpp`，再改三端结构体，最后更新 `docs/CONTRACT.md`
- 改因子口径：C++ 与 Python 必须同步修改，否则模型特征不一致
- 改策略参数：只改配置，不改逻辑
- 两个 C++ 入口不能同时参与编译
- 所有新文件必须遵循 `docs/COMMENT_STYLE.md` 的注释规范