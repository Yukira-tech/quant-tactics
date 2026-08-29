"""
@file __init__.py
@brief train 包对外导出入口

层级：
    backend/YukiPilot/train/

项目内绝对路径：
    backend/YukiPilot/train/__init__.py

模块作用：
    集中导出 build_dataset、build_feature_matrix 和 Trainer，
    供 YukiPilot 训练流水线直接使用。

使用者：
    YukiPilot/main.py --train 路径导入本包进行训练。

项目角色：
    训练层公共接口，隐藏 dataset 与 trainer 模块内部实现。

引入说明：
    从 .dataset 导入 build_dataset、build_feature_matrix。
    从 .trainer 导入 Trainer。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""

from .dataset import build_dataset, build_feature_matrix
from .trainer import Trainer

__all__ = ["build_dataset", "build_feature_matrix", "Trainer"]