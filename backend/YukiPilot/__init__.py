"""
@file __init__.py
@brief YukiPilot 包根

层级：
    backend/YukiPilot/

项目内绝对路径：
    backend/YukiPilot/__init__.py

模块作用：
    YukiPilot 是 StrategyEngine 的 Python 决策增强层。
    从 runtime_files/ 读取 C++ 产出的因子/信号/持仓/K线，
    用 Transformer 模型 + 规则融合生成最终决策，原子写回 agent_decisions/。

使用者：
    backend/ 下其他模块或外部脚本通过 import YukiPilot 访问本包。
    包内各子模块通过相对导入协作，不依赖本文件导出。

项目角色：
    包根标记文件，保持轻量，不主动导入任何子模块，
    避免循环导入和启动开销，方便各子模块独立开发联调。

引入说明：
    无任何导入。子模块使用相对导入（如 from .dataclass.schemas import ...），
    避免 dataclass/、langchain/ 目录名与标准库/三方库冲突。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""