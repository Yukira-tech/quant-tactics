"""
@file decision_loss.py
@brief DecisionLoss：分类损失 + 置信度校准损失

层级：
    backend/YukiPilot/loss/

项目内绝对路径：
    backend/YukiPilot/loss/decision_loss.py

模块作用：
    定义决策损失函数，由类别加权交叉熵和置信度校准 MSE 两部分组成。
    类别加权用于平衡 hold/buy/sell 样本不均，置信度校准让模型说真话。

使用者：
    YukiPilot/train/trainer.py 导入 DecisionLoss 用于模型训练。

项目角色：
    训练层核心损失函数，同时监督分类正确性和置信度可靠性。

引入说明：
    依赖 torch、torch.nn、torch.nn.functional。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""

import torch
import torch.nn as nn
import torch.nn.functional as F


class DecisionLoss(nn.Module):
    """决策损失 = cls_weight * 加权交叉熵 + conf_weight * 置信度校准 MSE。"""

    def __init__(
        self,
        cls_weight: float = 1.0,
        conf_weight: float = 0.2,
        class_weights: torch.Tensor | None = None,
    ):
        super().__init__()
        self.cls_weight = cls_weight
        self.conf_weight = conf_weight
        # 类别权重注册为 buffer，随模型设备迁移，不参与训练
        if class_weights is not None:
            self.register_buffer("class_weights", class_weights.float())
        else:
            self.class_weights = None

    def forward(
        self,
        logits: torch.Tensor,
        confidence: torch.Tensor,
        targets: torch.Tensor,
        correct_mask: torch.Tensor | None = None,
    ) -> torch.Tensor:
        """logits: [B, C]；confidence: [B, 1]；targets: [B] 整型标签；correct_mask 可选。

        correct_mask 为 None 时用 argmax(logits) == targets 现算。
        """
        # 1) 类别加权交叉熵
        cls_loss = F.cross_entropy(logits, targets, weight=self.class_weights)

        # 2) 置信度校准：目标置信度 = 预测正确 1.0 / 错误 0.0
        if correct_mask is None:
            correct_mask = logits.argmax(dim=-1) == targets
        conf_target = correct_mask.to(dtype=confidence.dtype)
        conf_loss = F.mse_loss(confidence.squeeze(-1), conf_target)

        return self.cls_weight * cls_loss + self.conf_weight * conf_loss