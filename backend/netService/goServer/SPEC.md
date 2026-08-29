# Go 层内部接口说明

维护者：Yukira  
适用：backend/netService/goServer/  
跨语言字段契约：以 `docs/CONTRACT.md` 与 `bridge.hpp` 为准

---

## 1. 模块定位

Go 服务器负责把 `runtime_files/` 下的运行时文件翻译成前端可用的 JSON，并通过 HTTP 和 WebSocket 提供给浏览器。

它不参与因子计算，不参与决策，只做：

- 读取运行时文件
- 合并为统一快照
- 提供 HTTP API
- 通过 WebSocket 实时推送

---

## 2. 文件结构

| 文件 | 职责 |
|------|------|
| `main.go` | 路由注册、HTTP 处理器、WebSocket、静态托管 |
| `snapshot.go` | 快照管理器、数据契约、读取运行时文件 |
| `watcher.go` | 运行时文件变化监听 |
| `go.mod` | Go 模块定义 |

---

## 3. 数据契约

以下结构体定义在 `snapshot.go`，字段名与 `bridge.hpp` 严格对齐。

### Kline

| 字段 | JSON | 类型 |
|------|------|------|
| Code | `code` | uint32 |
| Ymd | `ymd` | int32 |
| Open | `open` | float64 |
| High | `high` | float64 |
| Low | `low` | float64 |
| Close | `close` | float64 |
| Volume | `volume` | int64 |

### FactorOutput

| 字段 | JSON | 类型 |
|------|------|------|
| Symbol | `symbol` | string |
| Timestamp | `timestamp` | string |
| MaShort | `ma_short` | float64 |
| MaLong | `ma_long` | float64 |
| DonchianHigh | `donchian_high` | float64 |
| DonchianLow | `donchian_low` | float64 |
| Atr | `atr` | float64 |

### SignalOutput

| 字段 | JSON | 类型 |
|------|------|------|
| Symbol | `symbol` | string |
| Timestamp | `timestamp` | string |
| Signal | `signal` | string |
| Strength | `strength` | float64 |
| Source | `strategy_source` | string |

### PositionOutput

| 字段 | JSON | 类型 |
|------|------|------|
| Symbol | `symbol` | string |
| Timestamp | `timestamp` | string |
| Quantity | `quantity` | int64 |
| AvgCost | `avg_cost` | float64 |
| CurrentPrice | `current_price` | float64 |
| FloatingPnl | `floating_pnl` | float64 |
| InventoryAvailable | `inventory_available` | int64 |
| InventoryFrozen | `inventory_frozen` | int64 |

### TrainingProgress

| 字段 | JSON | 类型 |
|------|------|------|
| State | `state` | string，running / done / idle |
| Epoch | `epoch` | int |
| TotalEpochs | `total_epochs` | int |
| Batch | `batch` | int |
| TotalBatches | `total_batches` | int |
| Loss | `loss` | float64 |
| ValLoss | `val_loss` | float64 |
| ValAcc | `val_acc` | float64 |
| Speed | `speed` | float64，batch/秒 |
| EtaSeconds | `eta_seconds` | float64 |
| History | `history` | []EpochHistory |
| Timestamp | `timestamp` | string |

### EpochHistory

| 字段 | JSON | 类型 |
|------|------|------|
| Epoch | `epoch` | int |
| TrainLoss | `train_loss` | float64 |
| TrainAcc | `train_acc` | float64 |
| ValLoss | `val_loss` | float64 |
| ValAcc | `val_acc` | float64 |

### ThinkingEvent

| 字段 | JSON | 类型 |
|------|------|------|
| Type | `type` | string，status / tool / tool_result / text |
| Name | `name` | string，仅 tool 类有 |
| Text | `text` | string |
| T | `t` | string，如 `14:00:01` |

### ThinkingState

| 字段 | JSON | 类型 |
|------|------|------|
| Symbol | `symbol` | string |
| State | `state` | string，thinking / done |
| Started | `started` | string |
| Events | `events` | []ThinkingEvent |
| Final | `final` | json.RawMessage，done 时才有 |

### PortfolioSnapshot

| 字段 | JSON | 类型 |
|------|------|------|
| Timestamp | `timestamp` | string |
| TotalEquity | `total_equity` | float64 |
| TotalPnl | `total_pnl` | float64 |
| MaxDrawdown | `max_drawdown` | float64 |
| TargetPortfolio | `target_portfolio` | []TargetPosition |
| Signals | `signals` | []SignalOutput |
| Positions | `positions` | []PositionOutput |
| Training | `training` | *TrainingProgress，无训练时为空 |
| Thinking | `thinking` | []ThinkingState，LLM 未启用时为空 |

### TargetPosition

| 字段 | JSON | 类型 |
|------|------|------|
| Symbol | `symbol` | string |
| TargetQuantity | `target_quantity` | int64 |

---

## 4. 核心接口

### SnapshotManager

定义在 `snapshot.go`。

```go
func NewSnapshotManager() *SnapshotManager
func (sm *SnapshotManager) Build(runtimeDir string) error
func (sm *SnapshotManager) Get() PortfolioSnapshot
func (sm *SnapshotManager) LastUpdate() time.Time
```

`Build()` 读取全部运行时文件并合并成一份快照。  
`Get()` 返回缓存快照，读锁保护，不重新读文件。

### FileWatcher

定义在 `watcher.go`。

```go
func NewFileWatcher(runtimeDir string) *FileWatcher
func (fw *FileWatcher) HasChanged() bool
func (fw *FileWatcher) StartWatching(interval time.Duration, onChange func())
```

以轮询方式检查文件和目录修改时间。检测到变化就调用 `onChange`。

---

## 5. HTTP 端点

| 端点 | 方法 | 说明 |
|------|------|------|
| `/api/snapshot` | GET | 完整快照 |
| `/api/portfolio` | GET | 组合信息 |
| `/api/signals` | GET | 全部信号 |
| `/api/training` | GET | 训练进度，无训练时返回 `{"state":"idle"}` |
| `/api/thinking` | GET | 全部 Agent 思考状态，空时返回 `[]` |
| `/ws` | WebSocket | 每 2 秒推送一次最新快照 |
| `/` | GET | 302 重定向到 `/public/index.html` |

静态资源由 `/` 兜底，服务 `../../frontend` 目录下的文件。

---

## 6. WebSocket 行为

- 客户端连接后立即推送一次快照
- 之后每 2 秒推送一次
- 单客户端发送失败只断开该客户端，不影响服务器运行
- `CheckOrigin` 当前直接放行，仅适合本地开发

---

## 7. 数据流

```
runtime_files/
├── positions/{code}.json      → 持仓
├── signals/{code}.json        → 信号
├── portfolio/snapshot.json    → 组合快照
├── training/progress.json     → 训练进度
└── agent_thinking/{code}.json → 思考状态
        ↓
SnapshotManager.Build()
        ↓
PortfolioSnapshot
        ↓
HTTP JSON / WebSocket
        ↓
前端监控面板
```

---

## 8. 错误处理约定

- 文件不存在或 JSON 解析失败：跳过，不中断构建
- 训练进度文件不存在：`Training` 字段保持 nil
- 思考状态文件不存在：`Thinking` 字段保持 nil，接口返回空数组
- 排序保证 Signals 和 Positions 输出稳定
- 快照读取使用 `RWMutex`，写入使用写锁，读取使用读锁

任何单个文件损坏都不能让服务器崩溃。