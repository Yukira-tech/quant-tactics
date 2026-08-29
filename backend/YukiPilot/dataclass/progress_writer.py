"""
@file progress_writer.py
@brief 训练进度运行时文件写入

层级：
    backend/YukiPilot/dataclass/

项目内绝对路径：
    backend/YukiPilot/dataclass/progress_writer.py

模块作用：
    --train 训练时把 Trainer 的 batch/epoch 进度原子写到
    runtime_files/training/progress.json，供 Go watcher / 前端实时轮询。

使用者：
    YukiPilot/main.py --train 路径通过回调挂载本类。
    YukiPilot/train/trainer.py 训练时调用本类钩子。

项目角色：
    训练进度实时输出到 runtime_files，是前端训练面板的数据来源。

引入说明：
    依赖标准库 json、os、time、collections.deque、datetime。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""

import json
import os
import time
from collections import deque
from datetime import datetime

# batch 级进度写盘的最小间隔（节流，FE2_SPEC 1.1 要求最多每 200ms 一次）
_MIN_WRITE_INTERVAL_S = 0.2
# 速度/ETA 滑动平均窗口：只用最近 N 个 batch 耗时，口径与 Trainer 控制台进度条一致
_SPEED_WINDOW = 50


def _atomic_write_json(path: str, payload: dict) -> bool:
    """原子写 JSON：先写 {path}.tmp 再 os.replace；失败打印中文警告返回 False，不抛异常。"""
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp_path = path + ".tmp"
        with open(tmp_path, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=2)
            f.flush()
            os.fsync(f.fileno())  # 落盘后再替换，防止断电留下半截文件
        os.replace(tmp_path, path)
        return True
    except OSError as e:
        print(f"[警告] 原子写入失败 {path}：{e}")
        return False


class TrainingProgressWriter:
    """把训练进度按 FE2_SPEC 1.1 契约原子写入 training/progress.json。

    用法（main.py --train 路径）：
        writer = TrainingProgressWriter(agent_cfg.runtime_dir)
        trainer.train(X, y, on_train_start=writer.start,
                      on_batch_end=writer.on_batch_end,
                      on_epoch_end=writer.on_epoch_end)
        writer.finish()
    """

    def __init__(self, runtime_dir: str):
        """runtime_dir：runtime_files 目录的绝对路径（AgentConfig.runtime_dir 已锚定）。"""
        self.path = os.path.join(runtime_dir, "training", "progress.json")
        self._state = "idle"
        self._epoch = 0
        self._total_epochs = 0
        self._batch = 0
        self._total_batches = 0
        self._loss = 0.0
        self._history: list[dict] = []
        self._durations: deque = deque(maxlen=_SPEED_WINDOW)
        self._last_batch_ts: float | None = None
        self._last_write_ts = 0.0

    # ---- Trainer 钩子（签名与 Trainer.train 的回调一一对应） ----

    def start(self, total_epochs: int, total_batches: int):
        """训练开始钩子：记录 total_epochs/total_batches，强制写 state=running。"""
        self._state = "running"
        self._total_epochs = int(total_epochs)
        self._total_batches = int(total_batches)
        self._epoch = 1 if self._total_epochs > 0 else 0
        self._batch = 0
        self._loss = 0.0
        self._history = []
        self._durations.clear()
        self._last_batch_ts = time.monotonic()
        self._write(force=True)

    def on_batch_end(self, epoch, total_epochs, batch, total_batches, loss, eta_seconds):
        """Trainer on_batch_end 回调：更新 batch 进度，节流到每 200ms 最多写一次。

        eta_seconds 参数为 Trainer 的 epoch 内 ETA（未采用）；本文件写出的
        eta_seconds 是「剩余全部 batch × 滑动平均耗时」的全局口径，对前端更有意义。
        """
        now = time.monotonic()
        if self._last_batch_ts is not None:
            self._durations.append(now - self._last_batch_ts)
        self._last_batch_ts = now
        self._epoch = int(epoch)
        self._total_epochs = int(total_epochs)
        self._batch = int(batch)
        self._total_batches = int(total_batches)
        self._loss = float(loss)
        self._write(force=False)

    def on_epoch_end(self, epoch_metrics: dict):
        """Trainer on_epoch_end 回调：把该 epoch 指标 append 进 history 并强制写。

        epoch_metrics 字段：epoch/train_loss/train_acc/val_loss/val_acc
        （与 Trainer.train 的 history 记录一致）。
        """
        entry = {
            "epoch": int(epoch_metrics.get("epoch", self._epoch)),
            "train_loss": round(float(epoch_metrics.get("train_loss", 0.0)), 4),
            "train_acc": round(float(epoch_metrics.get("train_acc", 0.0)), 4),
            "val_loss": round(float(epoch_metrics.get("val_loss", 0.0)), 4),
            "val_acc": round(float(epoch_metrics.get("val_acc", 0.0)), 4),
        }
        self._history.append(entry)
        self._epoch = entry["epoch"]
        self._write(force=True)

    def finish(self):
        """训练结束：强制写 state=done（history 为完整逐 epoch 记录）。"""
        self._state = "done"
        self._write(force=True)

    # ---- 内部 ----

    def _speed(self) -> float:
        """滑动平均训练速度（batch/秒），无样本时为 0.0。"""
        total = sum(self._durations)
        if not self._durations or total <= 0:
            return 0.0
        return round(len(self._durations) / total, 2)

    def _eta_seconds(self):
        """剩余全部 batch × 滑动平均单 batch 耗时（秒，取整）；无法估计时为 None。"""
        if not self._durations or self._total_batches <= 0:
            return None
        avg = sum(self._durations) / len(self._durations)
        remaining = (self._total_epochs - self._epoch) * self._total_batches + (
            self._total_batches - self._batch
        )
        return max(0, int(round(remaining * avg)))

    def _payload(self) -> dict:
        """组装 FE2_SPEC 1.1 契约字段（key 与契约一字不差）。

        val_loss/val_acc 取最近一个已完成 epoch 的验证指标，尚无验证结果时为 None。
        """
        last = self._history[-1] if self._history else {}
        return {
            "state": self._state,
            "epoch": self._epoch,
            "total_epochs": self._total_epochs,
            "batch": self._batch,
            "total_batches": self._total_batches,
            "loss": round(self._loss, 4),
            "val_loss": last.get("val_loss"),
            "val_acc": last.get("val_acc"),
            "speed": self._speed(),
            "eta_seconds": self._eta_seconds(),
            "history": list(self._history),
            "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        }

    def _write(self, force: bool):
        """节流写盘：force=True 或距上次写盘超过 200ms 时才真正落盘。"""
        now = time.monotonic()
        if not force and now - self._last_write_ts < _MIN_WRITE_INTERVAL_S:
            return
        if _atomic_write_json(self.path, self._payload()):
            self._last_write_ts = now