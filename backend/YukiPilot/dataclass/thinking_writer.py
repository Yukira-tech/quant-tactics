"""
@file thinking_writer.py
@brief LLM 思考流运行时文件写入

层级：
    backend/YukiPilot/dataclass/

项目内绝对路径：
    backend/YukiPilot/dataclass/thinking_writer.py

模块作用：
    LLM 启用时，把 DecisionChain 的思考过程（状态 / 工具调用 / 流式文本）
    原子写到 runtime_files/agent_thinking/{symbol}.json，供 Go watcher / 前端实时轮询。

使用者：
    YukiPilot/main.py run_once 和 langchain/chain.py 依赖本类记录思考过程。

项目角色：
    LLM Agent 思考过程的可视化数据源，是前端 Agent 面板的数据来源。

引入说明：
    依赖标准库 os、datetime，
    依赖 .progress_writer 中的 _atomic_write_json。

维护记录：
    2026-08-29 初始创建，项目由 calcAgent 改名为 YukiPilot
"""

import os
from datetime import datetime

from .progress_writer import _atomic_write_json


class ThinkingWriter:
    """按 FE2_SPEC 1.2 契约原子写 runtime_files/agent_thinking/{symbol}.json。

    用法（main.py run_once / chain.explain）：
        writer = ThinkingWriter(agent_cfg.runtime_dir)
        writer.start(code)            # 每个标的一轮新决策前重置
        chain.explain(..., thinking=writer)   # 链内自动埋 status/tool/tool_result/text
        writer.finish(agent_decision.to_dict())  # 护栏校验后的最终决策
    """

    def __init__(self, runtime_dir: str):
        """runtime_dir：runtime_files 目录的绝对路径（AgentConfig.runtime_dir 已锚定）。"""
        self.out_dir = os.path.join(runtime_dir, "agent_thinking")
        self._symbol = ""
        self._state = "thinking"
        self._started = ""
        self._events: list[dict] = []
        self._final: dict | None = None

    # ---- 对外 API（每次变更都原子重写整个文件） ----

    def start(self, symbol: str):
        """开始一轮新决策：重置 events、state=thinking，立即原子写。"""
        self._symbol = str(symbol)
        self._state = "thinking"
        self._started = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self._events = []
        self._final = None
        self._write()

    def add_status(self, text: str):
        """追加一条状态事件（如「开始复核 600000」）。"""
        self._append({"type": "status", "text": str(text)})

    def add_tool(self, name: str):
        """追加一条工具调用事件（工具执行前调用）。"""
        self._append({"type": "tool", "name": str(name), "text": "调用工具"})

    def add_tool_result(self, name: str, summary: str):
        """追加一条工具结果事件（工具执行后调用，text 为返回 JSON 的摘要）。"""
        self._append({"type": "tool_result", "name": str(name), "text": str(summary)})

    def update_text(self, accumulated: str):
        """累积更新唯一一条 type=text 事件（流式 token 聚合后调用）。

        只保留一条 text 事件：已存在则原地更新 text/t，不存在则追加。
        """
        for event in self._events:
            if event.get("type") == "text":
                event["text"] = str(accumulated)
                event["t"] = self._now()
                break
        else:
            self._events.append(
                {"type": "text", "text": str(accumulated), "t": self._now()}
            )
        self._write()

    def finish(self, final_decision: dict):
        """收尾：state=done + final（护栏校验后的最终决策 dict），强制原子写。"""
        self._state = "done"
        self._final = dict(final_decision) if final_decision is not None else None
        self._write()

    # ---- 内部 ----

    @staticmethod
    def _now() -> str:
        """事件时间戳：HH:MM:SS（契约 1.2）。"""
        return datetime.now().strftime("%H:%M:%S")

    @property
    def _path(self) -> str:
        return os.path.join(self.out_dir, f"{self._symbol}.json")

    def _append(self, event: dict):
        event["t"] = self._now()
        self._events.append(event)
        self._write()

    def _payload(self) -> dict:
        """组装 FE2_SPEC 1.2 契约字段；final 仅 done 时出现。"""
        payload = {
            "symbol": self._symbol,
            "state": self._state,
            "started": self._started,
            "events": list(self._events),
        }
        if self._final is not None:
            payload["final"] = self._final
        return payload

    def _write(self):
        if not self._symbol:
            return  # start() 之前不落盘
        _atomic_write_json(self._path, self._payload())