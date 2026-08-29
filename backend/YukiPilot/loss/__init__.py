"""
@file __init__.py
@brief loss 包对外导出入口

层级：
    backend/YukiPilot/loss/

项目内绝对路径：
    backend/YukiPilot/loss/__init__.py

模块作用：
    集中导出 DecisionLoss，供 YukiPilot 训练模块直接使用。

使用者：
    YukiPilot/train/trainer.py 导入 DecisionLoss 作为训练损失函数。

项目角色：
    损失函数层公共接口，隐藏 loss 包内部实现细节。

引入说明：
    从 .decision_loss 导入 DecisionLoss。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""

from .decision_loss import DecisionLoss

__all__ = ["DecisionLoss"]