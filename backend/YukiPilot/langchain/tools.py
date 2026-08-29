"""
@file tools.py
@brief LLM 工具工厂

层级：
    backend/YukiPilot/langchain/

项目内绝对路径：
    backend/YukiPilot/langchain/tools.py

模块作用：
    把 skill 层纯函数绑定运行时上下文后暴露给大模型。
    build_tools(context) 用闭包把 reader、agent_cfg、当前 symbol 等上下文绑死，
    暴露给 LLM 的工具签名全部简单可序列化（无参或仅一个字符串参数），
    返回值一律为 JSON 字符串。

使用者：
    YukiPilot/langchain/chain.py 调用 build_tools 构建工具集。

项目角色：
    LLM 工具层，是 skill 纯函数与大模型之间的适配器。

引入说明：
    以模块别名方式从 ..skill 导入 tools 为 _skill，避免闭包递归。
    可选依赖 langchain_core，缺失时 build_tools 返回空列表。

维护记录：
    2026-08-29 初始创建，项目由 calcAgent 改名为 YukiPilot
"""
from __future__ import annotations

import json

# 以模块别名引用，避免与本文件内同名工具函数发生闭包递归
from ..skill import tools as _skill

# langchain_core 可选导入：任何失败都视为工具能力不可用
try:
    from langchain_core.tools import tool as _lc_tool
except Exception:  # noqa: BLE001
    _lc_tool = None


def _to_json_str(obj) -> str:
    """把 schema 对象 / dict / None 序列化为 JSON 字符串（中文不转义）。"""
    if obj is None:
        return "null"
    if hasattr(obj, "to_dict"):
        obj = obj.to_dict()
    try:
        return json.dumps(obj, ensure_ascii=False)
    except Exception:  # noqa: BLE001
        return str(obj)


def _err(msg: str) -> str:
    """工具内部异常的统一返回：JSON 错误字符串，绝不向 LLM 循环抛异常。"""
    return json.dumps({"error": msg}, ensure_ascii=False)


def build_tools(context: dict) -> list:
    """构建绑定运行时上下文的 LLM 工具列表。

    参数:
        context: 运行时上下文 dict，字段：
            reader            RuntimeReader（因子/信号/持仓缺失时回退重读）
            agent_cfg         AgentConfig
            symbol            当前标的代码
            factor/signal/position  已读取的快照对象（优先于 reader 重读）
            positions_count   当前持仓标的数量（仓位约束检查用）
            baseline_decision 确定性流水线的基线决策 buy/sell/hold
            baseline_confidence 基线置信度
            reason_parts      基线决策的中文依据短语列表
    返回:
        langchain_core 工具列表；langchain_core 不可用时返回空列表。
    """
    if _lc_tool is None:
        return []

    reader = context.get("reader")
    agent_cfg = context.get("agent_cfg")
    symbol = str(context.get("symbol", ""))

    def _snapshot_part(name: str):
        """优先使用上下文里已读取的快照对象，缺失时回退到 reader 重读。"""
        obj = context.get(name)
        if obj is not None or reader is None:
            return obj
        try:
            return getattr(reader, f"read_{name}")(symbol)
        except Exception:  # noqa: BLE001 - 读取失败按 None 处理
            return None

    @_lc_tool
    def get_factor_snapshot() -> str:
        """获取当前标的的因子、C++信号与持仓汇总（JSON）。复核决策前必须先调用本工具了解全貌。"""
        try:
            snapshot = _skill.get_factor_snapshot(symbol, reader) if reader is not None else {
                "code": symbol,
                "factor": _snapshot_part("factor"),
                "signal": _snapshot_part("signal"),
                "position": _snapshot_part("position"),
            }
            return _to_json_str(snapshot)
        except Exception as exc:  # noqa: BLE001
            return _err(f"获取因子快照失败：{exc}")

    @_lc_tool
    def check_signal_consistency() -> str:
        """检查 C++ 信号方向与因子暗示方向是否一致，返回 {consistent, detail} JSON。"""
        try:
            return _to_json_str(
                _skill.check_signal_consistency(_snapshot_part("factor"), _snapshot_part("signal"))
            )
        except Exception as exc:  # noqa: BLE001
            return _err(f"一致性检查失败：{exc}")

    @_lc_tool
    def check_risk() -> str:
        """评估当前持仓风险等级，返回 {level: low/mid/high, detail} JSON。下结论前必须调用。"""
        try:
            return _to_json_str(_skill.check_risk(_snapshot_part("position"), agent_cfg))
        except Exception as exc:  # noqa: BLE001
            return _err(f"风险检查失败：{exc}")

    @_lc_tool
    def check_position_constraint(decision: str) -> str:
        """检查拟执行决策是否触发持仓数量/仓位上限约束。

        参数:
            decision: 拟执行的决策，取值 buy / sell / hold。
        """
        try:
            decision = str(decision).strip().lower()
            if decision not in ("buy", "sell", "hold"):
                return _err(f"非法决策值 {decision!r}，只允许 buy/sell/hold")
            return _to_json_str(
                _skill.check_position_constraint(
                    symbol, decision, int(context.get("positions_count", 0)), agent_cfg
                )
            )
        except Exception as exc:  # noqa: BLE001
            return _err(f"仓位约束检查失败：{exc}")

    @_lc_tool
    def get_model_baseline() -> str:
        """获取确定性流水线（Transformer 模型 + 规则融合）的基线决策：decision/confidence/reason_parts JSON。可参考也可反驳，但反驳需在理由中说明。"""
        try:
            return _to_json_str(
                {
                    "decision": str(context.get("baseline_decision", "hold")),
                    "confidence": float(context.get("baseline_confidence", 0.0) or 0.0),
                    "reason_parts": list(context.get("reason_parts") or []),
                }
            )
        except Exception as exc:  # noqa: BLE001
            return _err(f"获取基线决策失败：{exc}")

    return [
        get_factor_snapshot,
        check_signal_consistency,
        check_risk,
        check_position_constraint,
        get_model_baseline,
    ]