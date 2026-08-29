# StrategyEngine

## 中文版

### 项目简介

StrategyEngine 是一个双均线 + CTA 混合策略引擎，面向 A 股多标的行情，采用 C++ / Python / Go 三语言协作，通过运行时文件系统解耦通信。

系统采用三层智能架构：C++ 规则层负责速度，Transformer 模型层负责模式识别，LLM Agent 层负责开放推理。每层失效自动降级到下一层，任意一层故障不拖垮整体。

> 当前状态：技术骨架完整，全链路可跑通。策略未经过严格回测，**不可直接用于实盘交易**。

### 核心特性

- **跨语言运行时文件通信**：各模块独立部署，通过原子写 JSON/JSONL 文件交换数据
- **C++ 高性能因子计算**：双均线、唐奇安通道、ATR，Kline 结构体 64 字节缓存行对齐
- **Python 决策增强层（YukiPilot）**：Transformer 模型推理、规则融合、LLM Agent 复核、代码级风控护栏
- **Go 实时服务**：HTTP API、WebSocket 推送、文件变化监听、前端静态托管
- **原生前端 SPA**：无框架，hash 路由四页切换，WebSocket 断线自动降级 HTTP 轮询
- **完整训练流水线**：K 线 JSONL 特征构建、滑窗标签、训练、checkpoint 管理
- **代码级安全护栏**：风控一票否决、置信度封顶、非法决策回退、审计痕迹

### 系统架构

```
历史K线 JSONL
  → C++ DataLoader 加载
  → C++ FactorEngine 计算因子
  → C++ StrategyEngine 生成信号
  → 检查 YukiPilot 决策覆盖
  → C++ PositionManager 更新持仓（T+1）
  → C++ DataWriter 原子写 runtime_files/
  → YukiPilot 读取文件 → 模型推理 → 规则融合 → LLM 复核 → 写回 agent_decisions/
  → Go watcher 检测变化 → 重建快照
  → Go 提供 HTTP API 与 WebSocket 推送
  → 前端渲染四页监控面板
```

### 目录结构

```
StrategyEngine/
├─ backend/
│  ├─ calcFactors/               # C++ 因子计算与规则策略
│  │  ├─ config/
│  │  ├─ fileopt/
│  │  ├─ runtime/
│  │  ├─ utils/
│  │  ├─ main_single.cpp         # 单文件版，推荐使用
│  │  ├─ main_modular.cpp        # 模块化版，实验性
│  │  └─ SPEC.md
│  ├─ YukiPilot/                 # Python 决策增强层
│  │  ├─ attention/
│  │  ├─ config/
│  │  ├─ dataclass/
│  │  ├─ langchain/
│  │  ├─ loss/
│  │  ├─ skill/
│  │  ├─ train/
│  │  ├─ checkpoints/
│  │  ├─ main.py
│  │  ├─ requirements.txt
│  │  ├─ SPEC.md
│  │  └─ TRAINING.md
│  ├─ netService/
│  │  ├─ goServer/
│  │  │  ├─ main.go
│  │  │  ├─ snapshot.go
│  │  │  ├─ watcher.go
│  │  │  ├─ go.mod
│  │  │  └─ SPEC.md
│  │  └─ bridge.hpp              # 跨语言 JSON 契约
│  └─ runtime_files/
│     ├─ agent_decisions/
│     ├─ factor_outputs/
│     ├─ klines/
│     ├─ portfolio/
│     ├─ positions/
│     └─ signals/
├─ frontend/
│  ├─ public/
│  │  └─ index.html
│  ├─ src/
│  │  ├─ api/
│  │  ├─ dom/
│  │  ├─ image/
│  │  ├─ style/
│  │  ├─ templates/
│  │  ├─ app.js
│  │  └─ router.js
│  └─ SPEC.md
└─ docs/
   ├─ ARCHITECTURE.md
   ├─ CONTRACT.md
   ├─ FEATURES.md
   ├─ TIMING.md
   ├─ COMMENT_STYLE.md
   └─ CHANGELOG.md
```

### 快速开始

#### 环境要求

- C++17 编译器（GCC / Clang / MSVC）
- Python 3.10+
- Go 1.21+
- 现代浏览器

#### 1. 准备 K 线数据

将 JSONL 格式的历史 K 线放入 `backend/runtime_files/klines/`，每行一条：

```json
{"code":600000,"ymd":20240102,"open":10.20,"high":10.80,"low":10.00,"close":10.50,"volume":1200000}
```

#### 2. 运行 C++ 核心

```bash
cd backend/calcFactors
g++ -std=c++17 -O2 -Wall -o engine main_single.cpp
./engine
```

可选参数：

```bash
./engine --replay 500    # 回放模式，每根K线间隔500ms
./engine --loop          # 回放后监听新K线追加
```

#### 3. 运行 Python 决策层

```bash
cd backend
pip install -r YukiPilot/requirements.txt
python -m YukiPilot.main --once      # 单次运行
python -m YukiPilot.main --train     # 先训练再进入循环
```

#### 4. 启动 Go 服务器

```bash
cd backend/netService/goServer
go run .
```

#### 5. 打开前端

浏览器访问：

```
http://localhost:8080
```

### 文档

| 文档 | 位置 |
|------|------|
| 架构文档 | `docs/ARCHITECTURE.md` |
| 跨语言契约 | `docs/CONTRACT.md` |
| 功能清单 | `docs/FEATURES.md` |
| 时序文档 | `docs/TIMING.md` |
| 注释规范 | `docs/COMMENT_STYLE.md` |
| C++ 接口说明 | `backend/calcFactors/SPEC.md` |
| Python 接口说明 | `backend/YukiPilot/SPEC.md` |
| 模型训练手册 | `backend/YukiPilot/TRAINING.md` |
| Go 接口说明 | `backend/netService/goServer/SPEC.md` |
| 前端接口说明 | `frontend/SPEC.md` |

### 技术栈

| 层 | 技术 | 说明 |
|----|------|------|
| 因子计算 | C++17 | 高性能、缓存行对齐、线程池 |
| 决策增强 | Python / PyTorch | Transformer 模型、LLM Agent |
| 网络服务 | Go | HTTP / WebSocket / 文件监听 |
| 前端 | 原生 HTML/CSS/JS | 无框架 SPA |
| 跨语言通信 | 文件系统 | JSON / JSONL + 原子写 |

### 当前状态

- 技术骨架完整，三语言全链路可跑通
- 三层智能架构与降级容错已落地
- 前端四页监控面板完成
- 训练进度与思考过程可视化完成
- **策略未经过严格历史回测，模型未在真实 A 股数据上训练，不可用于实盘**

### 后续路线

- [ ] 接入真实 A 股数据源
- [ ] 补全 A 股交易规则（整手、T+1、印花税）
- [ ] 加入沪深 300 基准对比
- [ ] 用真实数据重训模型
- [ ] 完善 Go 服务鉴权
- [ ] 文件监听换 fsnotify
- [ ] 将 `main_single.cpp` 中的类迁移到 `.hpp`，使 `main_modular.cpp` 成为唯一正式入口

---

## English Version

### Introduction

StrategyEngine is a dual moving average + CTA hybrid strategy engine for A-share multi-symbol markets, built with C++, Python, and Go, communicating through a runtime file system.

The system uses a three-layer intelligence architecture: C++ rules for speed, Transformer models for pattern recognition, and LLM Agent for open-ended reasoning. Each layer automatically degrades to the next on failure, preventing any single point of failure from bringing down the whole system.

> Current status: technical skeleton complete, full pipeline runnable. Strategies have not been rigorously backtested and **must not be used for live trading**.

### Core Features

- **Cross-language runtime file communication**: modules independently deployed, exchanging data through atomic JSON/JSONL writes
- **C++ high-performance factor computation**: dual moving averages, Donchian channels, ATR, 64-byte cache-line-aligned Kline
- **Python decision enhancement layer (YukiPilot)**: Transformer inference, rule fusion, LLM Agent review, code-level risk guards
- **Go real-time service**: HTTP API, WebSocket push, file change monitoring, frontend static hosting
- **Native SPA frontend**: framework-free, hash routing with four pages, WebSocket auto-fallback to HTTP polling
- **Full training pipeline**: K-line JSONL feature construction, sliding-window labeling, training, checkpoint management
- **Code-level safety guards**: risk veto, confidence cap, invalid decision fallback, audit trail

### System Architecture

```
Historical K-line JSONL
  → C++ DataLoader
  → C++ FactorEngine
  → C++ StrategyEngine
  → YukiPilot decision override check
  → C++ PositionManager (T+1)
  → C++ DataWriter atomic writes to runtime_files/
  → YukiPilot reads files → model inference → rule fusion → LLM review → writes agent_decisions/
  → Go watcher detects changes → rebuilds snapshot
  → Go serves HTTP API and WebSocket
  → Frontend renders four-page monitoring dashboard
```

### Directory Structure

```
StrategyEngine/
├─ backend/
│  ├─ calcFactors/               # C++ factor computation and rule strategies
│  │  ├─ config/
│  │  ├─ fileopt/
│  │  ├─ runtime/
│  │  ├─ utils/
│  │  ├─ main_single.cpp         # single-file version, recommended
│  │  ├─ main_modular.cpp        # modular version, experimental
│  │  └─ SPEC.md
│  ├─ YukiPilot/                 # Python decision enhancement layer
│  │  ├─ attention/
│  │  ├─ config/
│  │  ├─ dataclass/
│  │  ├─ langchain/
│  │  ├─ loss/
│  │  ├─ skill/
│  │  ├─ train/
│  │  ├─ checkpoints/
│  │  ├─ main.py
│  │  ├─ requirements.txt
│  │  ├─ SPEC.md
│  │  └─ TRAINING.md
│  ├─ netService/
│  │  ├─ goServer/
│  │  │  ├─ main.go
│  │  │  ├─ snapshot.go
│  │  │  ├─ watcher.go
│  │  │  ├─ go.mod
│  │  │  └─ SPEC.md
│  │  └─ bridge.hpp              # cross-language JSON contract
│  └─ runtime_files/
│     ├─ agent_decisions/
│     ├─ factor_outputs/
│     ├─ klines/
│     ├─ portfolio/
│     ├─ positions/
│     └─ signals/
├─ frontend/
│  ├─ public/
│  │  └─ index.html
│  ├─ src/
│  │  ├─ api/
│  │  ├─ dom/
│  │  ├─ image/
│  │  ├─ style/
│  │  ├─ templates/
│  │  ├─ app.js
│  │  └─ router.js
│  └─ SPEC.md
└─ docs/
   ├─ ARCHITECTURE.md
   ├─ CONTRACT.md
   ├─ FEATURES.md
   ├─ TIMING.md
   ├─ COMMENT_STYLE.md
   └─ CHANGELOG.md
```

### Quick Start

#### Requirements

- C++17 compiler (GCC / Clang / MSVC)
- Python 3.10+
- Go 1.21+
- Modern browser

#### 1. Prepare K-line Data

Place JSONL historical K-line files into `backend/runtime_files/klines/`, one object per line:

```json
{"code":600000,"ymd":20240102,"open":10.20,"high":10.80,"low":10.00,"close":10.50,"volume":1200000}
```

#### 2. Run C++ Core

```bash
cd backend/calcFactors
g++ -std=c++17 -O2 -Wall -o engine main_single.cpp
./engine
```

Optional flags:

```bash
./engine --replay 500    # replay mode with 500ms interval
./engine --loop          # keep watching for new K-lines after replay
```

#### 3. Run Python Decision Layer

```bash
cd backend
pip install -r YukiPilot/requirements.txt
python -m YukiPilot.main --once      # single run
python -m YukiPilot.main --train     # train first, then enter loop
```

#### 4. Start Go Server

```bash
cd backend/netService/goServer
go run .
```

#### 5. Open Frontend

Visit:

```
http://localhost:8080
```

### Documentation

| Document | Location |
|----------|----------|
| Architecture | `docs/ARCHITECTURE.md` |
| Cross-language contract | `docs/CONTRACT.md` |
| Features | `docs/FEATURES.md` |
| Timing | `docs/TIMING.md` |
| Comment style | `docs/COMMENT_STYLE.md` |
| C++ interface | `backend/calcFactors/SPEC.md` |
| Python interface | `backend/YukiPilot/SPEC.md` |
| Training guide | `backend/YukiPilot/TRAINING.md` |
| Go interface | `backend/netService/goServer/SPEC.md` |
| Frontend interface | `frontend/SPEC.md` |

### Tech Stack

| Layer | Tech | Notes |
|-------|------|-------|
| Factor computation | C++17 | high performance, cache-line alignment, thread pool |
| Decision enhancement | Python / PyTorch | Transformer model, LLM Agent |
| Network service | Go | HTTP / WebSocket / file monitoring |
| Frontend | vanilla HTML/CSS/JS | framework-free SPA |
| Cross-language communication | filesystem | JSON / JSONL with atomic writes |

### Current Status

- Technical skeleton complete, full three-language pipeline runnable
- Three-layer intelligence architecture with graceful degradation implemented
- Four-page frontend monitoring dashboard completed
- Training progress and thought process visualization completed
- **Strategies not rigorously backtested, models not trained on real A-share data, not suitable for live trading**

### Roadmap

- [ ] Integrate real A-share data source
- [ ] Implement full A-share trading rules (lot size, T+1, stamp duty)
- [ ] Add CSI 300 benchmark comparison
- [ ] Retrain models on real data
- [ ] Add Go server authentication
- [ ] Replace file polling with fsnotify
- [ ] Migrate classes from `main_single.cpp` into `.hpp`, making `main_modular.cpp` the only official entry point
