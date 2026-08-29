"""
@file __init__.py
@brief 数据契约包对外导出入口

层级：
    backend/YukiPilot/dataclass/

项目内绝对路径：
    backend/YukiPilot/dataclass/__init__.py

模块作用：
    集中导出全部数据契约类与 IO 读写器，
    包括 Kline、FactorOutput、SignalOutput、PositionOutput、AgentDecision，
    以及 RuntimeReader、DecisionWriter、TrainingProgressWriter、ThinkingWriter。

使用者：
    YukiPilot/main.py 导入 RuntimeReader、DecisionWriter。
    YukiPilot/train/ 相关模块导入数据契约类。

项目角色：
    数据契约层公共接口，是 YukiPilot 与运行时文件之间的数据交换入口。

引入说明：
    从 .io 导入 DecisionWriter、RuntimeReader。
    从 .progress_writer 导入 TrainingProgressWriter。
    从 .schemas 导入各数据契约类。
    从 .thinking_writer 导入 ThinkingWriter。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""

from .io import DecisionWriter, RuntimeReader
from .progress_writer import TrainingProgressWriter
from .schemas import (
    AgentDecision,
    FactorOutput,
    Kline,
    PositionOutput,
    SignalOutput,
)
from .thinking_writer import ThinkingWriter

__all__ = [
    "Kline",
    "FactorOutput",
    "SignalOutput",
    "PositionOutput",
    "AgentDecision",
    "RuntimeReader",
    "DecisionWriter",
    "TrainingProgressWriter",
    "ThinkingWriter",
]