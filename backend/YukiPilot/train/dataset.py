"""
@file dataset.py
@brief 训练数据集构建

层级：
    backend/YukiPilot/train/

项目内绝对路径：
    backend/YukiPilot/train/dataset.py

模块作用：
    从 runtime_files/klines/ 读取 JSONL K线数据，构建因子特征矩阵，
    滑窗切分并生成三分类标签，输出训练数据集。

使用者：
    YukiPilot/train/trainer.py 调用本模块构建训练数据。
    YukiPilot/main.py --train 路径通过本模块加载数据集。

项目角色：
    YukiPilot 训练流水线的数据入口，负责把原始K线转换为模型可用的训练样本。

引入说明：
    依赖标准库 json、math、pathlib，依赖 numpy。
    从 ..config.agent_config 导入 ModelConfig、TrainConfig。
    从 ..dataclass.schemas 导入 Kline。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""

import json
import math
from pathlib import Path

import numpy as np

from ..config.agent_config import ModelConfig, TrainConfig
from ..dataclass.schemas import Kline

# 特征维度与固定顺序（SPEC 3.3）：[open, high, low, close, log1p(volume),
#   ma_short, ma_long, donchian_high, donchian_low, atr]
FEATURE_NAMES = [
    "open", "high", "low", "close", "log_volume",
    "ma_short", "ma_long", "donchian_high", "donchian_low", "atr",
]
NUM_FEATURES = len(FEATURE_NAMES)  # 10


def build_feature_matrix(
    klines: list[Kline],
    short_win: int = 5,
    long_win: int = 20,
    donchian_win: int = 20,
    atr_win: int = 14,
) -> np.ndarray:
    """逐行计算因子特征矩阵。

    klines 按时间升序；ohlc 四列按窗口首日收盘价归一化（除以首根 close），
    因子列保留原值。返回 [T, 10] 的 float32 数组。
    """
    n = len(klines)
    feats = np.zeros((n, NUM_FEATURES), dtype=np.float32)
    if n == 0:
        return feats

    opens = np.array([k.open for k in klines], dtype=np.float64)
    highs = np.array([k.high for k in klines], dtype=np.float64)
    lows = np.array([k.low for k in klines], dtype=np.float64)
    closes = np.array([k.close for k in klines], dtype=np.float64)
    volumes = np.array([k.volume for k in klines], dtype=np.float64)

    # 归一化基准：窗口首日收盘价（防除零）
    base = closes[0] if closes[0] != 0 else 1.0

    # 先算逐根真实波幅 TR
    tr = np.zeros(n, dtype=np.float64)
    for i in range(n):
        if i == 0:
            tr[i] = highs[i] - lows[i]  # 首根无前收，TR = high - low
        else:
            tr[i] = max(
                highs[i] - lows[i],
                abs(highs[i] - closes[i - 1]),
                abs(lows[i] - closes[i - 1]),
            )

    for i in range(n):
        # 简单移动平均：数据不足窗口时为 0.0（与 C++ 侧一致）
        ma_short = closes[i - short_win + 1: i + 1].mean() if i >= short_win - 1 else 0.0
        ma_long = closes[i - long_win + 1: i + 1].mean() if i >= long_win - 1 else 0.0
        # 唐奇安通道：最近 donchian_win 根（含当根）的最高/最低价
        if i >= donchian_win - 1:
            donchian_high = highs[i - donchian_win + 1: i + 1].max()
            donchian_low = lows[i - donchian_win + 1: i + 1].min()
        else:
            donchian_high = 0.0
            donchian_low = 0.0
        # ATR：TR 的简单移动平均
        atr = tr[i - atr_win + 1: i + 1].mean() if i >= atr_win - 1 else 0.0

        feats[i] = (
            opens[i] / base,
            highs[i] / base,
            lows[i] / base,
            closes[i] / base,
            math.log1p(volumes[i]),
            ma_short,
            ma_long,
            donchian_high,
            donchian_low,
            atr,
        )
    return feats


def _label_from_return(ret: float, train_cfg: TrainConfig) -> int:
    """未来收益 → 三分类标签：> +2% → buy=1；< -2% → sell=2；否则 hold=0。"""
    if ret > train_cfg.buy_threshold:
        return 1
    if ret < train_cfg.sell_threshold:
        return 2
    return 0


def _read_klines_jsonl(path: Path) -> list[Kline]:
    """读取单个 klines JSONL 文件（每行一个 JSON 对象），坏行跳过并告警。"""
    klines: list[Kline] = []
    try:
        with open(path, "r", encoding="utf-8") as f:
            for line_no, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    klines.append(Kline.from_dict(json.loads(line)))
                except (json.JSONDecodeError, TypeError, ValueError) as e:
                    print(f"[警告] {path.name} 第 {line_no} 行解析失败，已跳过: {e}")
    except OSError as e:
        print(f"[警告] 读取 K 线文件失败 {path}: {e}")
    return klines


def build_dataset(
    runtime_dir: str,
    model_cfg: ModelConfig,
    train_cfg: TrainConfig,
) -> tuple[np.ndarray, np.ndarray]:
    """遍历 runtime_dir/klines/*.jsonl，滑窗切序列并打三分类标签。

    窗口长度为 model_cfg.seq_len；标签 = 窗口末尾向后 forward_days 的收益，
    > buy_threshold → 1(buy)，< sell_threshold → 2(sell)，否则 0(hold)。
    窗口末尾之后不足 forward_days 的样本丢弃。

    返回 (X, y)：X [N, seq_len, 10] float32，y [N] int64。
    """
    kline_dir = Path(runtime_dir) / "klines"
    seq_len = model_cfg.seq_len
    forward_days = train_cfg.forward_days

    xs: list[np.ndarray] = []
    ys: list[int] = []

    if not kline_dir.is_dir():
        print(f"[警告] K 线目录不存在: {kline_dir}，返回空数据集")
    else:
        for path in sorted(kline_dir.glob("*.jsonl")):
            klines = _read_klines_jsonl(path)
            klines.sort(key=lambda k: k.ymd)  # 保证按日期升序
            n = len(klines)
            if n < seq_len + forward_days:
                print(
                    f"[警告] {path.name} 仅 {n} 根 K 线，"
                    f"不足 seq_len({seq_len}) + forward_days({forward_days})，已跳过"
                )
                continue

            feats = build_feature_matrix(klines)
            closes = np.array([k.close for k in klines], dtype=np.float64)

            # 滑窗：[start, start+seq_len) 为输入，窗口末尾 end = start+seq_len-1
            for start in range(0, n - seq_len - forward_days + 1):
                end = start + seq_len - 1
                base_close = closes[end]
                if base_close == 0:
                    continue  # 防除零
                future_ret = closes[end + forward_days] / base_close - 1.0
                xs.append(feats[start: start + seq_len])
                ys.append(_label_from_return(future_ret, train_cfg))

    if not xs:
        print("[警告] 没有可用训练样本（K 线数据太少），返回空数据集")
        return (
            np.zeros((0, seq_len, NUM_FEATURES), dtype=np.float32),
            np.zeros((0,), dtype=np.int64),
        )

    X = np.stack(xs).astype(np.float32)
    y = np.array(ys, dtype=np.int64)
    counts = np.bincount(y, minlength=3)
    print(
        f"[数据集] 共 {len(y)} 个样本，标签分布 hold={counts[0]} buy={counts[1]} sell={counts[2]}"
    )
    return X, y