"""
@file __init__.py
@brief attention 包对外导出入口

层级：
    backend/YukiPilot/attention/

项目内绝对路径：
    backend/YukiPilot/attention/__init__.py

模块作用：
    集中导出 FactorTransformer、PositionalEncoding、TransformerEncoderBlock，
    供 YukiPilot 其他模块直接使用。

使用者：
    YukiPilot/train/trainer.py 导入 FactorTransformer 进行训练。
    YukiPilot/main.py 导入 FactorTransformer 进行推理。

项目角色：
    模型层的公共接口，隐藏 attention 包内部实现细节。

引入说明：
    从 .components 导入 PositionalEncoding、TransformerEncoderBlock。
    从 .model 导入 FactorTransformer。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""

from .components import PositionalEncoding, TransformerEncoderBlock
from .model import FactorTransformer

__all__ = ["FactorTransformer", "PositionalEncoding", "TransformerEncoderBlock"]