# YukiPilot 内部接口说明

维护者：Yukira  
适用：Python 决策增强层  
跨语言字段契约：以 `docs/CONTRACT.md` 为准

---

## 1. 模块边界

| 模块 | 路径 | 职责 |
|------|------|------|
| `config` | `config/` | 配置类，含校验 |
| `dataclass` | `dataclass/` | 数据结构 + 文件 IO |
| `attention` | `attention/` | Transformer 模型 |
| `loss` | `loss/` | 损失函数 |
| `skill` | `skill/` | 纯函数决策逻辑 |
| `langchain` | `langchain/` | LLM Agent 复核 |
| `train` | `train/` | 数据集 + 训练 |

包内全部相对导入，`__init__.py` 只 re-export。

---

## 2. 数据契约

### Kline

| 字段 | 类型 |
|------|------|
| `code` | int |
| `ymd` | int |
| `open` | float |
| `high` | float |
| `low` | float |
| `close` | float |
| `volume` | int |

方法：`from_dict()` / `to_dict()`

### FactorOutput

| 字段 | 类型 |
|------|------|
| `symbol` | str |
| `timestamp` | str |
| `ma_short` | float |
| `ma_long` | float |
| `donchian_high` | float |
| `donchian_low` | float |
| `atr` | float |

方法：`from_dict()` / `to_dict()`

### SignalOutput

| 字段 | 类型 |
|------|------|
| `symbol` | str |
| `timestamp` | str |
| `signal` | str，枚举 buy/sell/hold/short/cover |
| `strength` | float |
| `strategy_source` | str |

方法：`from_dict()` / `to_dict()` / `is_buy()` / `is_sell()`

### PositionOutput

| 字段 | 类型 |
|------|------|
| `symbol` | str |
| `timestamp` | str |
| `quantity` | int |
| `avg_cost` | float |
| `current_price` | float |
| `floating_pnl` | float |
| `inventory_available` | int |
| `inventory_frozen` | int |

方法：`from_dict()` / `to_dict()`

### AgentDecision

| 字段 | 类型 |
|------|------|
| `symbol` | str |
| `timestamp` | str |
| `final_decision` | str，枚举 buy/sell/hold |
| `confidence` | float |
| `reason` | str |

方法：`from_dict()` / `to_dict()` / `validate()`

---

## 3. IO 层

### RuntimeReader

```python
def list_codes() -> list[str]
def read_factor(code) -> FactorOutput | None
def read_signal(code) -> SignalOutput | None
def read_position(code) -> PositionOutput | None
def read_klines(code, limit=None) -> list[Kline]
```

### DecisionWriter

```python
def write(decision: AgentDecision) -> str
```

### TrainingProgressWriter

```python
def start(total_epochs, total_batches)
def on_batch_end(epoch, total_epochs, batch, total_batches, loss, eta_seconds)
def on_epoch_end(epoch_metrics)
def finish()
```

### ThinkingWriter

```python
def start(symbol)
def add_status(text)
def add_tool(name)
def add_tool_result(name, summary)
def update_text(accumulated)
def finish(final_decision)
```

---

## 4. 模型层

### FactorTransformer

```python
def __init__(cfg: ModelConfig)
def forward(x: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]
def predict(x) -> tuple[np.ndarray, np.ndarray]
```

输入：`[B, T, 10]`  
输出：`logits [B, 3]`，`confidence [B, 1]`

结构：Linear → PositionalEncoding → 2×EncoderBlock → final_norm → 双头

---

## 5. 损失层

### DecisionLoss

```python
def __init__(cls_weight=1.0, conf_weight=0.2, class_weights=None)
def forward(logits, confidence, targets, correct_mask=None) -> torch.Tensor
```

组成：加权交叉熵 + 置信度校准 MSE

---

## 6. 技能层

```python
def get_factor_snapshot(code, reader) -> dict
def check_signal_consistency(factor, signal) -> dict
def check_risk(position, agent_cfg) -> dict
def check_position_constraint(code, decision, positions_count, agent_cfg) -> dict
def fuse_decision(cpp_signal, model_probs, model_conf, agent_cfg) -> tuple[str, float, list[str]]
def validate_llm_decision(llm_decision, baseline, risk_info, agent_cfg) -> AgentDecision | None
```

关键：

- `fuse_decision` 是确定性决策融合，产出基线决策
- `validate_llm_decision` 是 LLM 决策护栏，不暴露给大模型

---

## 7. LLM Agent 层

### build_tools

```python
def build_tools(context: dict) -> list
```

暴露五个工具：因子快照、信号一致性、风险检查、仓位约束、基线决策

### DecisionChain

```python
def available() -> bool
def explain(...) -> AgentDecision | None
def explain_template(symbol, final_decision, confidence, reason_parts) -> AgentDecision
```

手写 tool-calling 循环，最大 6 轮，30 秒超时，异常返回 None

---

## 8. 训练层

### build_feature_matrix

```python
def build_feature_matrix(klines, short_win=5, long_win=20, donchian_win=20, atr_win=14) -> np.ndarray
```

### build_dataset

```python
def build_dataset(runtime_dir, model_cfg, train_cfg) -> tuple[np.ndarray, np.ndarray]
```

### Trainer

```python
def __init__(model_cfg, train_cfg)
def train(X, y, on_batch_end=None, on_train_start=None, on_epoch_end=None) -> dict
def evaluate(X, y) -> dict
```

---

## 9. 主循环

### run_once

对每只股票：

```
读K线 → 特征 → 模型推理 → fuse_decision → 风险/仓位检查 → LLM复核 → 护栏 → 写回
```

### 三种运行方式

```bash
python -m YukiPilot.main --once
python -m YukiPilot.main --train
python -m YukiPilot.main
```

---

## 10. 错误处理约定

- 文件缺失或 JSON 损坏：警告，返回 None 或空列表
- 单标的处理失败：警告，跳过，不影响其他标的
- LLM 异常、超时、解析失败：返回 None，降级规则模板
- checkpoint 缺失或损坏：警告，用随机初始化兜底

任何情况下主循环不得崩溃。