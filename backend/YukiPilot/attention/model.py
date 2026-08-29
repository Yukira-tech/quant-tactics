"""
@file model.py
@brief FactorTransformer 多因子时序分类模型

层级：
    backend/YukiPilot/attention/

项目内绝对路径：
    backend/YukiPilot/attention/model.py

模块作用：
    定义 FactorTransformer 模型，输入多因子时序数据，
    输出三分类决策 logits 和 sigmoid 置信度。

使用者：
    YukiPilot/train/trainer.py 导入本模块进行训练。
    YukiPilot/main.py 导入本模块进行推理。

项目角色：
    YukiPilot 决策层的核心模型，负责从因子序列中学习买卖信号。

引入说明：
    依赖 numpy、torch、torch.nn。
    从 ..config.agent_config 导入 ModelConfig。
    从 .components 导入 PositionalEncoding、TransformerEncoderBlock。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""

import numpy as np
import torch
import torch.nn as nn

from ..config.agent_config import ModelConfig
from .components import PositionalEncoding, TransformerEncoderBlock


class FactorTransformer(nn.Module):
    """因子序列 → 三分类决策 + 置信度 的轻量 Transformer。"""

    def __init__(self, cfg: ModelConfig):
        super().__init__()
        self.cfg = cfg

        # 输入投影：num_features 维因子 → d_model 维隐空间
        self.input_proj = nn.Linear(cfg.num_features, cfg.d_model)
        # 位置编码预留余量，避免推理时序列略长于训练窗口直接报错
        self.pos_enc = PositionalEncoding(
            d_model=cfg.d_model, max_len=cfg.seq_len + 64, dropout=cfg.dropout
        )
        # N 层 pre-norm 编码块
        self.blocks = nn.ModuleList(
            TransformerEncoderBlock(
                d_model=cfg.d_model,
                nhead=cfg.nhead,
                dim_feedforward=cfg.dim_feedforward,
                dropout=cfg.dropout,
            )
            for _ in range(cfg.num_layers)
        )
        # pre-norm 结构末端需要一个最终 LayerNorm
        self.final_norm = nn.LayerNorm(cfg.d_model)
        # 双头：分类头 + 置信度头
        self.head_cls = nn.Linear(cfg.d_model, cfg.num_classes)
        self.head_conf = nn.Linear(cfg.d_model, 1)

    def forward(self, x: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
        """x: [B, T, num_features] → (logits [B, num_classes], confidence [B, 1] 经 sigmoid)。"""
        h = self.input_proj(x)          # [B, T, d_model]
        h = self.pos_enc(h)
        for block in self.blocks:
            h = block(h)
        h = self.final_norm(h[:, -1])   # 取最后时间步 [B, d_model]
        logits = self.head_cls(h)                       # [B, num_classes]
        confidence = torch.sigmoid(self.head_conf(h))   # [B, 1]，值域 (0, 1)
        return logits, confidence

    @torch.no_grad()
    def predict(self, x) -> tuple[np.ndarray, np.ndarray]:
        """推理便捷方法：输入 np.ndarray 或 torch.Tensor [B, T, num_features] 或 [T, num_features]。

        返回 (probs, confidence)：均为 np.ndarray，
        probs [B, num_classes] 为 softmax 概率，confidence [B, 1] 为 sigmoid 置信度。
        """
        self.eval()
        # 取模型所在设备，兼容 CPU/GPU
        device = next(self.parameters()).device
        if isinstance(x, np.ndarray):
            x = torch.from_numpy(x)
        x = x.to(device=device, dtype=torch.float32)
        if x.dim() == 2:
            x = x.unsqueeze(0)  # 单条序列补 batch 维
        logits, confidence = self.forward(x)
        probs = torch.softmax(logits, dim=-1)
        return probs.cpu().numpy(), confidence.cpu().numpy()