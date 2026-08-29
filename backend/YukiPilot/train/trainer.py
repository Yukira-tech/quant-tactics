"""
@file trainer.py
@brief Trainer：FactorTransformer 训练与评估

层级：
    backend/YukiPilot/train/

项目内绝对路径：
    backend/YukiPilot/train/trainer.py

模块作用：
    封装 FactorTransformer 的训练与评估流程。
    支持 8:2 时序切分、Adam 优化、DecisionLoss 损失、
    CPU 友好进度条、按验证集 loss 保存最佳 checkpoint。

使用者：
    YukiPilot/main.py --train 路径导入 Trainer 进行训练。

项目角色：
    YukiPilot 训练流水线的执行模块，负责模型参数优化与 checkpoint 管理。

引入说明：
    依赖标准库 sys、time、collections.deque、pathlib。
    依赖 numpy、torch、torch.nn、torch.utils.data。
    从 ..attention.model 导入 FactorTransformer。
    从 ..config.agent_config 导入 ModelConfig、TrainConfig。
    从 ..loss.decision_loss 导入 DecisionLoss。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""

import sys
import time
from collections import deque
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

from ..attention.model import FactorTransformer
from ..config.agent_config import ModelConfig, TrainConfig
from ..loss.decision_loss import DecisionLoss

# 少于该样本数时认为数据太少：仍可训练但打印警告
MIN_SAMPLES_WARN = 32

# 进度条格数（████████░░░░░░░░ 共 16 格）
_PROGRESS_BAR_CELLS = 16
# 训练速度滑动平均窗口：只用最近 N 个 batch 的耗时，避免早期抖动长期影响 ETA
_SPEED_WINDOW = 50


class Trainer:
    """模型训练器：封装设备选择、训练循环、评估与 checkpoint 保存。"""

    def __init__(self, model_cfg: ModelConfig, train_cfg: TrainConfig):
        self.model_cfg = model_cfg
        self.train_cfg = train_cfg
        # 无 GPU 自动 CPU
        self.device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        self.model = FactorTransformer(model_cfg).to(self.device)
        self.optimizer = torch.optim.Adam(self.model.parameters(), lr=train_cfg.lr)

    def _to_tensor(self, X, y) -> tuple[torch.Tensor, torch.Tensor]:
        """numpy/list → torch.Tensor，统一 float32 / int64。"""
        X_t = torch.as_tensor(np.asarray(X), dtype=torch.float32)
        y_t = torch.as_tensor(np.asarray(y), dtype=torch.long)
        return X_t, y_t

    def _make_class_weights(self, y_train: torch.Tensor) -> torch.Tensor | None:
        """按训练集标签频率的倒数构造类别权重（hold 样本多，需加权平衡）。"""
        counts = torch.bincount(y_train, minlength=self.model_cfg.num_classes).float()
        if (counts == 0).any() or counts.sum() == 0:
            return None  # 存在缺失类别时退化为不加权，避免除零
        weights = counts.sum() / (counts * self.model_cfg.num_classes)
        return weights.to(self.device)

    @staticmethod
    def _accuracy(logits: torch.Tensor, targets: torch.Tensor) -> float:
        """分类准确率。"""
        return (logits.argmax(dim=-1) == targets).float().mean().item()

    def _run_epoch(self, loader: DataLoader, criterion: DecisionLoss, train_mode: bool,
                   on_batch=None):
        """跑一个 epoch，返回 (平均 loss, 平均 acc)。

        on_batch: 可选回调，签名 on_batch(batch_idx, total_batches, batch_loss)，
        每个 batch 结束后调用一次（1 起始）。训练进度条/外部推送都挂在它上面；
        验证阶段调用方不传 on_batch，因此 val 不计入进度条。
        """
        self.model.train(train_mode)
        total_loss, total_acc, total_n = 0.0, 0.0, 0
        total_batches = len(loader)
        for batch_idx, (xb, yb) in enumerate(loader, 1):
            xb, yb = xb.to(self.device), yb.to(self.device)
            with torch.set_grad_enabled(train_mode):
                logits, confidence = self.model(xb)
                loss = criterion(logits, confidence, yb)
                if train_mode:
                    self.optimizer.zero_grad()
                    loss.backward()
                    self.optimizer.step()
            bs = yb.size(0)
            total_loss += loss.item() * bs
            total_acc += self._accuracy(logits, yb) * bs
            total_n += bs
            if on_batch is not None:
                on_batch(batch_idx, total_batches, loss.item())
        if total_n == 0:
            return 0.0, 0.0
        return total_loss / total_n, total_acc / total_n

    def _save_checkpoint(self, path: str):
        """保存模型权重，自动创建父目录。"""
        ckpt_path = Path(path)
        ckpt_path.parent.mkdir(parents=True, exist_ok=True)
        torch.save(
            {
                "model_state_dict": self.model.state_dict(),
                "model_cfg": vars(self.model_cfg),
            },
            ckpt_path,
        )

    @staticmethod
    def _render_progress_line(epoch, total_epochs, batch, total_batches, loss,
                              avg_dur, eta_seconds):
        """内置控制台进度条：\r 覆写单行实时刷新（零依赖，仅一次 write + flush）。

        形如：
        [epoch 12/150] ████████░░░░░░░░ 67% | batch 43/64 | loss 0.4521 | 3.2 batch/s | ETA 00:41
        """
        if total_batches > 0:
            filled = int(round(_PROGRESS_BAR_CELLS * batch / total_batches))
            pct = int(100 * batch / total_batches)
        else:
            filled, pct = 0, 100
        filled = max(0, min(_PROGRESS_BAR_CELLS, filled))
        bar = "█" * filled + "░" * (_PROGRESS_BAR_CELLS - filled)
        speed = 1.0 / avg_dur if avg_dur > 0 else 0.0
        mm, ss = int(eta_seconds // 60), int(eta_seconds % 60)
        sys.stdout.write(
            f"\r[epoch {epoch}/{total_epochs}] {bar} {pct}% | "
            f"batch {batch}/{total_batches} | loss {loss:.4f} | "
            f"{speed:.1f} batch/s | ETA {mm:02d}:{ss:02d}"
        )
        sys.stdout.flush()

    def _make_batch_progress(self, epoch, total_epochs, on_batch_end,
                             use_console_bar):
        """构造训练 epoch 的 batch 级进度处理器；不需要进度时返回 None。

        计时口径：相邻两次 batch 结束的墙钟差即该 batch 耗时，滑动窗口取最近
        _SPEED_WINDOW 个；ETA = 剩余 batch 数 × 滑动平均耗时。
        """
        if on_batch_end is None and not use_console_bar:
            return None
        durations: deque = deque(maxlen=_SPEED_WINDOW)
        last = [time.monotonic()]

        def _handle(batch, total_batches, loss):
            now = time.monotonic()
            durations.append(now - last[0])
            last[0] = now
            avg_dur = sum(durations) / len(durations)
            eta = max(0.0, (total_batches - batch) * avg_dur)
            if on_batch_end is not None:
                on_batch_end(epoch, total_epochs, batch, total_batches, loss, eta)
            if use_console_bar:
                self._render_progress_line(
                    epoch, total_epochs, batch, total_batches, loss, avg_dur, eta
                )

        return _handle

    def train(self, X, y, on_batch_end=None, on_train_start=None, on_epoch_end=None) -> dict:
        """训练模型。返回含逐 epoch 历史与最佳验证指标的字典。

        参数:
            X, y: 特征矩阵与标签（numpy / list）。
            on_batch_end: 可选 batch 级回调，签名
                on_batch_end(epoch, total_epochs, batch, total_batches, loss, eta_seconds)。
                为 None 且 TrainConfig.stream_progress=True 时使用内置控制台进度条
                （\r 覆写单行）；stream_progress=False 且未传回调时退回纯 epoch 日志。
            on_train_start: 可选训练开始钩子，签名 on_train_start(total_epochs, total_batches)，
                在数据加载器构建完成后、首个 epoch 前调用一次（FE2 进度文件需要总 batch 数）。
            on_epoch_end: 可选 epoch 结束钩子，签名 on_epoch_end(epoch_metrics)，
                每个 epoch 的 train/val 指标写入 history 后调用一次，
                epoch_metrics 与 history 记录同构（epoch/train_loss/train_acc/val_loss/val_acc）。
        """
        X_t, y_t = self._to_tensor(X, y)
        n = len(y_t)
        if n == 0:
            print("[警告] 训练数据为空，跳过训练")
            return {"history": [], "best_val_loss": None, "best_val_acc": None, "checkpoint": None}
        if n < MIN_SAMPLES_WARN:
            print(f"[警告] 训练样本仅 {n} 条（< {MIN_SAMPLES_WARN}），模型可能无法收敛")

        split = max(1, int(n * 0.8))
        split = min(split, n - 1) if n >= 2 else n
        X_train, y_train = X_t[:split], y_t[:split]
        X_val, y_val = X_t[split:], y_t[split:]

        class_weights = self._make_class_weights(y_train)
        criterion = DecisionLoss(class_weights=class_weights).to(self.device)

        batch_size = max(1, min(self.train_cfg.batch_size, len(y_train)))
        train_loader = DataLoader(
            TensorDataset(X_train, y_train), batch_size=batch_size, shuffle=True
        )
        val_loader = DataLoader(TensorDataset(X_val, y_val), batch_size=batch_size)

        best_val_loss = float("inf")
        best_val_acc = 0.0
        history = []
        ckpt_path = self.train_cfg.checkpoint_path

        stream_console = bool(getattr(self.train_cfg, "stream_progress", True))

        if on_train_start is not None:
            on_train_start(self.train_cfg.epochs, len(train_loader))

        for epoch in range(1, self.train_cfg.epochs + 1):
            on_batch = self._make_batch_progress(
                epoch, self.train_cfg.epochs, on_batch_end, stream_console
            )
            train_loss, train_acc = self._run_epoch(
                train_loader, criterion, train_mode=True, on_batch=on_batch
            )
            if on_batch is not None and stream_console:
                sys.stdout.write("\n")
                sys.stdout.flush()
            val_loss, val_acc = self._run_epoch(val_loader, criterion, train_mode=False)
            history.append(
                {"epoch": epoch, "train_loss": train_loss, "train_acc": train_acc,
                 "val_loss": val_loss, "val_acc": val_acc}
            )
            if on_epoch_end is not None:
                on_epoch_end(dict(history[-1]))
            print(
                f"[epoch {epoch:>3}/{self.train_cfg.epochs}] "
                f"train_loss={train_loss:.4f} train_acc={train_acc:.4f} | "
                f"val_loss={val_loss:.4f} val_acc={val_acc:.4f}"
            )
            if val_loss < best_val_loss:
                best_val_loss = val_loss
                best_val_acc = val_acc
                self._save_checkpoint(ckpt_path)

        print(f"[训练完成] 最佳 val_loss={best_val_loss:.4f} val_acc={best_val_acc:.4f}，"
              f"checkpoint 已保存至 {ckpt_path}")
        return {
            "history": history,
            "best_val_loss": best_val_loss,
            "best_val_acc": best_val_acc,
            "checkpoint": ckpt_path,
        }

    @torch.no_grad()
    def evaluate(self, X, y) -> dict:
        """评估模型。返回 {loss, acc, n}；空数据返回 None 指标并告警。"""
        X_t, y_t = self._to_tensor(X, y)
        n = len(y_t)
        if n == 0:
            print("[警告] 评估数据为空")
            return {"loss": None, "acc": None, "n": 0}

        self.model.eval()
        criterion = DecisionLoss()
        batch_size = max(1, min(self.train_cfg.batch_size, n))
        loader = DataLoader(TensorDataset(X_t, y_t), batch_size=batch_size)

        total_loss, total_acc = 0.0, 0.0
        for xb, yb in loader:
            xb, yb = xb.to(self.device), yb.to(self.device)
            logits, confidence = self.model(xb)
            loss = criterion(logits, confidence, yb)
            bs = yb.size(0)
            total_loss += loss.item() * bs
            total_acc += self._accuracy(logits, yb) * bs
        return {"loss": total_loss / n, "acc": total_acc / n, "n": n}