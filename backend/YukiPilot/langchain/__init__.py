"""
@file __init__.py
@brief langchain 包对外导出入口

层级：
    backend/YukiPilot/langchain/

项目内绝对路径：
    backend/YukiPilot/langchain/__init__.py

模块作用：
    集中导出 tool-calling LLM 决策 Agent 所需的 DecisionChain、
    DECISION_PROMPT 和 build_tools，供 YukiPilot 其他模块使用。

使用者：
    YukiPilot/main.py 导入 DecisionChain 构建 LLM 决策链。
    YukiPilot/langchain/chain.py 内部使用本包导出的组件。

项目角色：
    LLM 决策层公共接口，隐藏 langchain 包内部实现细节。

引入说明：
    从 .chain 导入 DecisionChain。
    从 .prompts 导入 DECISION_PROMPT。
    从 .tools 导入 build_tools。

维护记录：
    2026-08-29 初始创建，项目由 calcAgent 改名为 YukiPilot
"""

from .chain import DecisionChain
from .prompts import DECISION_PROMPT
from .tools import build_tools

__all__ = ["DecisionChain", "DECISION_PROMPT", "build_tools"]