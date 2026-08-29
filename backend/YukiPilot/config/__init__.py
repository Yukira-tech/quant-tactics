"""
@file __init__.py
@brief 配置包对外导出入口

层级：
    backend/YukiPilot/config/

项目内绝对路径：
    backend/YukiPilot/config/__init__.py

模块作用：
    集中导出 AgentConfig、ModelConfig、TrainConfig 三个配置类，
    供 YukiPilot 其他模块通过包级导入直接使用。

使用者：
    YukiPilot/main.py 导入 AgentConfig 和 TrainConfig。
    YukiPilot/attention/model.py 导入 ModelConfig。

项目角色：
    配置层公共接口，隐藏 agent_config 模块内部实现。

引入说明：
    从 .agent_config 导入 AgentConfig、ModelConfig、TrainConfig。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""

from .agent_config import AgentConfig, ModelConfig, TrainConfig

__all__ = ["AgentConfig", "ModelConfig", "TrainConfig"]