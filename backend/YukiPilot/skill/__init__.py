"""
@file __init__.py
@brief skill 包对外导出入口

层级：
    backend/YukiPilot/skill/

项目内绝对路径：
    backend/YukiPilot/skill/__init__.py

模块作用：
    集中导出决策技能工具集中的纯函数，并提供 ALL_SKILLS 清单，
    供 YukiPilot 主循环和 langchain 工具工厂统一使用。

使用者：
    YukiPilot/main.py 导入 fuse_decision 等技能函数。
    YukiPilot/langchain/tools.py 通过 ALL_SKILLS 或直接导入技能函数。

项目角色：
    决策技能层公共接口，所有技能函数均为无副作用的纯函数。

引入说明：
    从 .tools 导入全部技能函数。
    注意：validate_llm_decision 已导出，但未加入 ALL_SKILLS，
    因为该函数用于护栏校验而非 LLM 工具调用，不应暴露给大模型。

维护记录：
    2026-08-29 初始创建，项目由 calcAgent 改名为 YukiPilot
"""

from .tools import (
    get_factor_snapshot,
    check_signal_consistency,
    check_risk,
    check_position_constraint,
    fuse_decision,
    validate_llm_decision,
)

# 全部技能清单：便于 LangChain 侧统一包装为 Tool
# 注意：validate_llm_decision 不在此清单，它是代码级护栏，不应暴露给 LLM
ALL_SKILLS = [
    get_factor_snapshot,
    check_signal_consistency,
    check_risk,
    check_position_constraint,
    fuse_decision,
]

__all__ = [
    "get_factor_snapshot",
    "check_signal_consistency",
    "check_risk",
    "check_position_constraint",
    "fuse_decision",
    "validate_llm_decision",
    "ALL_SKILLS",
]