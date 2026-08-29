# TRAINING.md — FactorTransformer 模型训练手册

> 适用模块：`backend/YukiPilot/`（attention / loss / train）  
> 阅读对象：需要训练或重训决策模型的人，无需理解 C++ 侧实现。

---

## 一、模型介绍

FactorTransformer 是 YukiPilot 决策层的小型时序分类模型，负责从多因子序列中学习买卖信号。

### 模型结构

```
输入 [B, T, 10]
  → Linear(10 → 64)
  → 正弦位置编码
  → 2 层 pre-norm Transformer Encoder
  → 取最后时间步
  → 分类头 Linear(64 → 3)
  → 置信度头 Linear(64 → 1) + sigmoid
```

- 输入：10 维因子序列，时间长度 `seq_len`（默认 32）
- 输出：3 类 logits（0=hold，1=buy，2=sell）+ 置信度 [0,1]
- 默认参数量 **68,036**（约 260 KB）
- CPU 上训练和推理都很快，无需 GPU

### 模型定位

模型不是独立决策者，它只产出“概率 + 置信度”，然后由 `fuse_decision()` 与 C++ 信号融合。  
模型置信度不足时，系统自动降级到规则决策，模型不会单独决定买卖。

---

## 二、训练流水线总览

```
runtime_files/klines/{code}.jsonl     （原始日K线，唯一输入）
        ↓  build_feature_matrix()     train/dataset.py
逐根计算 10 维特征（与 C++ FactorEngine 同口径）
        ↓  build_dataset()            train/dataset.py
滑窗切序列 + 未来 N 日收益打三分类标签
        ↓  Trainer.train()            train/trainer.py
8:2 时序切分 → Adam → DecisionLoss → 保存最佳 checkpoint
        ↓
YukiPilot/checkpoints/factor_transformer.pt
        ↓  main.py 加载
推理：predict() → softmax 概率 + 置信度 → fuse_decision 融合
```

---

## 三、数据准备（最重要的一步）

### 3.1 数据格式

把 K 线放进 `backend/runtime_files/klines/`，**每只股票一个文件**，文件名即代码：

```
runtime_files/klines/
├─ 600000.jsonl
├─ 000001.jsonl
└─ ...
```

每行一个 JSON 对象（JSONL），字段与 `bridge.hpp` 对齐，**按 ymd 升序**：

```json
{"code":600000,"ymd":20240101,"open":10.00,"high":10.20,"low":9.95,"close":10.10,"volume":1000000}
```

### 3.2 数据量要求（血泪经验，见第九节）

| 规模 | 样本量估算 | 结果 |
|------|-----------|------|
| 1 票 × 60 根 | ~24 条 | 标签极端不平衡，模型坍缩 |
| 1 票 × 250 根（1 年） | ~220 条 | 勉强能跑，仍太薄 |
| 10 票 × 250 根 | ~2,200 条 | 最低可用 |
| 50 票 × 500 根 | ~24,000 条 | 推荐 |

单票最短要求：`seq_len + forward_days + 最长因子窗口` ≈ 32+5+20 ≈ **60 根**（低于此直接 0 样本）。

数据来源：C++ 侧 DataLoader 的输出、akshare/tushare 导出、或手工整理，格式对上即可。

---

## 四、特征工程（自动完成，了解即可）

`build_feature_matrix(klines)` 逐根输出 **10 维特征**，顺序固定：

| # | 特征 | 说明 |
|---|------|------|
| 0-3 | open / high / low / close | 按窗口首根收盘价归一化（除法） |
| 4 | log1p(volume) | 成交量对数压缩 |
| 5 | ma_short | 5 日简单均线 |
| 6 | ma_long | 20 日简单均线 |
| 7 | donchian_high | 20 日唐奇安上轨 |
| 8 | donchian_low | 20 日唐奇安下轨 |
| 9 | atr | 14 日 ATR（TR 的 SMA，与 C++ 同口径） |

因子窗口期不足时输出 0.0（与 C++ FactorEngine 行为一致）。  
改窗口：传参 `build_feature_matrix(klines, short_win=5, long_win=20, donchian_win=20, atr_win=14)`，默认值与 `StrategyConfig.hpp` 对齐。

---

## 五、标签规则

对每条长度为 `seq_len` 的窗口，看窗口末尾之后 `forward_days` 天的收益：

```
未来收益 = close[t + forward_days] / close[t] - 1

> +buy_threshold  (默认 +2%)  →  标签 1 = buy
< sell_threshold  (默认 -2%)  →  标签 2 = sell
其余                          →  标签 0 = hold
```

窗口末尾不足 `forward_days` 的样本自动丢弃（没有未来数据可对照）。

---

## 六、开始训练

### 6.1 一条命令

```bash
cd backend
python -m YukiPilot.main --train           # 训练完自动进入决策轮询
python -m YukiPilot.main --train --once    # 训练完只跑一轮决策就退出（推荐验证用）
```

### 6.2 训练参数（`config/agent_config.py` → TrainConfig）

| 参数 | 默认 | 说明 |
|------|------|------|
| `lr` | 1e-3 | Adam 学习率 |
| `epochs` | 20 | 数据少时可加到 100~150 |
| `batch_size` | 64 | |
| `forward_days` | 5 | 标签视野，见第五节 |
| `buy_threshold` | 0.02 | 涨 2% 标 buy；噪声多可放宽到 0.03 |
| `sell_threshold` | -0.02 | 跌 2% 标 sell |
| `checkpoint_path` | `checkpoints/factor_transformer.pt` | 相对包目录解析，与 cwd 无关 |

模型结构参数在 `ModelConfig`：`seq_len=32`、`d_model=64`、`nhead=4`、`num_layers=2`、`dim_feedforward=128`、`dropout=0.1`。

### 6.3 训练日志解读

```
[数据集] 共 2200 个样本，标签分布 hold=800 buy=750 sell=650
[epoch 1/20] train_loss=1.0985 train_acc=0.4123 | val_loss=1.0521 val_acc=0.4386
...
[训练完成] 最佳 val_loss=0.4213 val_acc=0.7850，checkpoint 已保存至 ...
```

- 先看标签分布：任何一类占比 >70% 都要警惕
- 8:2 切分是顺序切分（前 80% 训练、后 20% 验证），时序数据不打乱，防止未来泄漏
- val_acc 在 0.4~0.6 是正常水平（三分类随机基线 0.33），追求方向正确率而非绝对值
- 类别权重自动按训练集标签频率倒数构造；某类样本缺失时自动退化为不加权

### 6.4 损失函数（`loss/decision_loss.py`）

```
DecisionLoss = 1.0 × 类别加权交叉熵
             + 0.2 × 置信度校准 MSE（预测正确→置信度逼近1，错误→逼近0）
```

第二项让置信度头说真话，Agent 链路里 `min_confidence=0.6` 的熔断全靠它校准。

---

## 七、验证与推理

```bash
python -m YukiPilot.main --once
cat ../runtime_files/agent_decisions/600000.json
```

```json
{
  "symbol": "600000",
  "timestamp": "2024-01-15 14:30:00",
  "final_decision": "buy",
  "confidence": 0.85,
  "reason": "双均线金叉 + 模型与C++信号同向做多"
}
```

加载安全机制：checkpoint 里的 `model_cfg` 与当前配置不一致会警告并回退随机初始化，不会静默出错。改了 `ModelConfig` 必须重训。

---

## 八、重训与更新流程

```bash
# 1. 把新 K 线追加/替换到 runtime_files/klines/*.jsonl
# 2. 直接重训
python -m YukiPilot.main --train --once
# 3. 观察 val_acc 与新决策是否合理，不合理回滚：
git checkout -- backend/YukiPilot/checkpoints/
```

建议：每次重训前备份旧 checkpoint；A股风格漂移时用最近 1~2 年数据重训。

---

## 九、实战踩坑记录（2026-08 实测）

1. **窗口长度决定你“看没看见”行情。**  
   单票 60 根 + `seq_len=32`：上涨段只有 20 根，窗口还没凑满行情就结束了 → 24 个样本全是 sell 标签 → 模型只会说卖。窗口必须短于你要捕捉的最短趋势。

2. **类别不平衡 + 逆频加权 = 可能反向坍缩。**  
   实测 buy=1/hold=3/sell=36 时，唯一的 buy 样本被加权放大几十倍，模型坍缩成“永远 buy”。  
   解法优先级：加数据 > 调宽阈值均衡标签 > 改权重。

3. **置信度熔断是最后一道防线。**  
   坍缩模型置信度 ~0.54 低于 `min_confidence=0.6`，Agent 自动降级 hold——训练再烂也不会乱下单。  
   但你要能从日志里看出它“一直在 hold”就是没学到东西。

4. **归一化抹掉了绝对价格。**  
   ohlc 按窗口首日收盘归一化，模型只看“形状”不看价位——这是有意的（防过拟合价格段），  
   但意味着 10 元的票和 100 元的票对模型是同一种语言。

---

## 十、FAQ

**Q：需要 GPU 吗？**  
不需要。6.8 万参数，CPU 训练 2 万样本约 1~2 分钟。有 CUDA 自动用。

**Q：能用 5 分钟 K 线吗？**  
能，格式不变。注意把 `seq_len`/`forward_days` 按新周期重新理解（32 根 5 分钟 ≈ 半交易日），并相应调阈值。

**Q：hold 样本太多怎么办？**  
调宽 `buy_threshold/sell_threshold`（如 ±3%），或在 `dataset.py` 里对 hold 欠采样。

**Q：训练报错 “K线不足”？**  
该票数据 < `seq_len + forward_days`，会自动跳过不影响其他票；补齐数据或删文件。

**Q：如何确认因子和 C++ 一致？**  
`train/dataset.py` 的因子口径与 `FactorEngine.hpp` 逐行对拍过。C++ 改窗口参数时，同步改 `build_feature_matrix` 的传参。