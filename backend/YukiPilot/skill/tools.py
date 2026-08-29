"""
@file tools.py
@brief 决策技能工具集

层级：
    backend/YukiPilot/skill/

项目内绝对路径：
    backend/YukiPilot/skill/tools.py

模块作用：
    提供量化决策所需的全部纯函数技能：
    - 因子快照汇总
    - C++ 信号与因子方向一致性检查
    - 持仓风险检查
    - 仓位约束检查
    - 模型与 C++ 信号决策融合
    - LLM 决策代码级护栏校验

使用者：
    YukiPilot/main.py 直接调用 fuse_decision、check_risk 等函数。
    YukiPilot/langchain/tools.py 通过 build_tools 将部分函数暴露给 LLM。

项目角色：
    决策技能层核心，所有函数无副作用，是 YukiPilot 决策逻辑的确定性底座。

引入说明：
    依赖 numpy，
    依赖 ..dataclass.schemas 中的 AgentDecision。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，新增 validate_llm_decision 护栏
"""

from __future__ import annotations

import numpy as np

from ..dataclass.schemas import AgentDecision

# 模型输出类别到决策的映射（与 ModelConfig.num_classes=3 对齐：0=hold 1=buy 2=sell）
CLASS_TO_DECISION = {0: "hold", 1: "buy", 2: "sell"}

# C++ 信号枚举到方向枚举的映射：cover 视为做多方向，short 视为做空方向
_SIGNAL_TO_DIR = {
    "buy": "buy",
    "cover": "buy",
    "sell": "sell",
    "short": "sell",
    "hold": "hold",
}


def _signal_fields(cpp_signal) -> tuple[str, float]:
    """从 cpp_signal 提取 (signal, strength)，兼容 SignalOutput 对象 / dict / 字符串 / None。"""
    if cpp_signal is None:
        return "hold", 0.0
    if isinstance(cpp_signal, str):
        return cpp_signal, 0.5
    if isinstance(cpp_signal, dict):
        return str(cpp_signal.get("signal", "hold")), float(cpp_signal.get("strength", 0.5) or 0.5)
    # SignalOutput 等 dataclass 对象
    return (
        str(getattr(cpp_signal, "signal", "hold")),
        float(getattr(cpp_signal, "strength", 0.5) or 0.5),
    )


def get_factor_snapshot(code, reader) -> dict:
    """汇总某标的的因子 + 信号 + 持仓为一个 dict（缺失项为 None）。

    参数:
        code: 标的代码，如 "600000"。
        reader: RuntimeReader 实例。
    返回:
        {"code", "factor", "signal", "position"}，后三项为 to_dict() 结果或 None。
    """
    factor = reader.read_factor(code)
    signal = reader.read_signal(code)
    position = reader.read_position(code)
    return {
        "code": code,
        "factor": factor.to_dict() if factor is not None else None,
        "signal": signal.to_dict() if signal is not None else None,
        "position": position.to_dict() if position is not None else None,
    }


def check_signal_consistency(factor, signal) -> dict:
    """检查 C++ 信号方向与因子暗示的方向是否一致。

    因子方向判定（FactorOutput 字段）：
      - ma_short > ma_long       → 多头（双均线金叉/多头排列）
      - ma_short < ma_long       → 空头（双均线死叉）
      - ma_short >= donchian_high → 唐奇安上轨突破（加强多头）
      - ma_short <= donchian_low  → 唐奇安下轨破位（加强空头）

    参数:
        factor: FactorOutput 对象或 dict，可为 None。
        signal: SignalOutput 对象 / dict / 字符串，可为 None。
    返回:
        {"consistent": bool, "detail": str}，detail 为中文短语说明。
    """
    sig_name, _ = _signal_fields(signal)
    sig_dir = _SIGNAL_TO_DIR.get(sig_name, "hold")

    if factor is None:
        return {"consistent": True, "detail": "无因子数据，跳过一致性检查"}

    def _get(obj, name, default=0.0):
        if isinstance(obj, dict):
            return float(obj.get(name, default) or default)
        return float(getattr(obj, name, default) or default)

    ma_short = _get(factor, "ma_short")
    ma_long = _get(factor, "ma_long")
    dc_high = _get(factor, "donchian_high")
    dc_low = _get(factor, "donchian_low")

    reasons = []
    if ma_short > ma_long:
        factor_dir = "buy"
        reasons.append("双均线金叉")
        if dc_high > 0 and ma_short >= dc_high:
            reasons.append("唐奇安上轨突破")
    elif ma_short < ma_long:
        factor_dir = "sell"
        reasons.append("双均线死叉")
        if dc_low > 0 and ma_short <= dc_low:
            reasons.append("唐奇安下轨破位")
    else:
        factor_dir = "hold"
        reasons.append("均线粘合无方向")

    if sig_dir == "hold":
        return {"consistent": True, "detail": "C++信号为hold，无需方向一致性校验"}
    if factor_dir == "hold":
        return {"consistent": False, "detail": f"因子无明确方向，C++信号为{sig_name}"}

    consistent = factor_dir == sig_dir
    if consistent:
        detail = f"C++信号({sig_name})与因子方向一致：" + "、".join(reasons)
    else:
        detail = f"C++信号({sig_name})与因子方向冲突：" + "、".join(reasons)
    return {"consistent": consistent, "detail": detail}


def check_risk(position, agent_cfg) -> dict:
    """检查单个持仓的风险等级。

    规则：
      - 无持仓 → low
      - 浮亏比例（floating_pnl / 持仓成本）≤ -10% → high（深度亏损）
      - 浮亏比例 ≤ -5%  → mid
      - 其余 → low
      - 可用库存为 0 且持仓 > 0（全部冻结）→ 至少 mid（流动性风险）

    参数:
        position: PositionOutput 对象或 dict，可为 None。
        agent_cfg: AgentConfig（预留阈值扩展）。
    返回:
        {"level": "low" | "mid" | "high", "detail": str}
    """
    if position is None:
        return {"level": "low", "detail": "无持仓"}

    def _get(obj, name, default=0.0):
        if isinstance(obj, dict):
            return obj.get(name, default)
        return getattr(obj, name, default)

    quantity = int(_get(position, "quantity", 0) or 0)
    if quantity <= 0:
        return {"level": "low", "detail": "无持仓"}

    avg_cost = float(_get(position, "avg_cost", 0.0) or 0.0)
    floating_pnl = float(_get(position, "floating_pnl", 0.0) or 0.0)
    available = int(_get(position, "inventory_available", quantity) or 0)

    cost = max(avg_cost * quantity, 1e-9)
    loss_ratio = floating_pnl / cost

    if loss_ratio <= -0.10:
        return {"level": "high", "detail": f"浮亏比例{loss_ratio:.1%}超过-10%止损线"}
    if loss_ratio <= -0.05:
        return {"level": "mid", "detail": f"浮亏比例{loss_ratio:.1%}接近止损线"}
    if available <= 0:
        return {"level": "mid", "detail": "可用库存为0，全部冻结，存在流动性风险"}
    return {"level": "low", "detail": f"浮盈比例{loss_ratio:.1%}，风险可控"}


def check_position_constraint(code, decision, positions_count, agent_cfg) -> dict:
    """检查决策是否触发持仓数量 / 仓位上限约束。

    规则：
      - 新开 buy 时持仓数已达 max_positions → 不允许；
      - sell / hold 不受持仓数限制；
      - max_position_ratio 为单标的仓位上限提示（实际仓位核算在 C++ 侧）。

    参数:
        code: 标的代码。
        decision: 拟执行决策 buy/sell/hold。
        positions_count: 当前已有持仓的标的数量。
        agent_cfg: AgentConfig。
    返回:
        {"allowed": bool, "detail": str}
    """
    max_positions = int(getattr(agent_cfg, "max_positions", 10))
    max_ratio = float(getattr(agent_cfg, "max_position_ratio", 0.2))

    if decision == "buy" and positions_count >= max_positions:
        return {
            "allowed": False,
            "detail": f"持仓数{positions_count}已达上限{max_positions}，禁止新开买入",
        }
    return {
        "allowed": True,
        "detail": f"持仓数{positions_count}/{max_positions}，单标的仓位上限{max_ratio:.0%}，约束通过",
    }


def fuse_decision(cpp_signal, model_probs, model_conf, agent_cfg) -> tuple[str, float, list[str]]:
    """核心决策融合：模型 argmax 概率 + C++ 信号一致性加权。

    规则：
      1. 模型置信度 < agent_cfg.min_confidence → 原则上降级 hold；
         但 C++ 信号为 buy/sell（short→sell、cover→buy）且强度 ≥ 0.7 时，
         采用 C++ 信号方向兜底（置信度 = 强度 × 0.75）；
      2. 模型与 C++ 信号同向（且非 hold）→ 按 C++ 信号强度加成置信度；
      3. 双方冲突（一买一卖）→ 取置信高的一方并按 0.7 降权；
      4. 模型 hold → hold（C++ 信号仅作参考记录）；
      5. C++ hold、模型有方向 → 采用模型决策。

    参数:
        cpp_signal: SignalOutput 对象 / dict / 字符串 / None。
        model_probs: 各类别概率，形如 [3] 或 [1, 3]（softmax 后）。
        model_conf: 模型置信度标量（sigmoid 输出），可为 [1, 1]。
        agent_cfg: AgentConfig。
    返回:
        (final_decision, confidence, reason_parts)
        final_decision ∈ {buy, sell, hold}；confidence ∈ [0, 1]；reason_parts 为中文短语列表。
    """
    probs = np.asarray(model_probs, dtype=float).reshape(-1)
    if probs.size != 3:
        return "hold", 0.0, ["模型输出维度异常，降级hold"]
    conf = float(np.asarray(model_conf, dtype=float).reshape(-1)[0])
    conf = float(np.clip(conf, 0.0, 1.0))

    model_cls = int(np.argmax(probs))
    model_decision = CLASS_TO_DECISION[model_cls]
    model_prob = float(probs[model_cls])

    sig_name, sig_strength = _signal_fields(cpp_signal)
    cpp_dir = _SIGNAL_TO_DIR.get(sig_name, "hold")
    sig_strength = float(np.clip(sig_strength, 0.0, 1.0))

    min_conf = float(getattr(agent_cfg, "min_confidence", 0.6))
    reason_parts: list[str] = []

    if conf < min_conf:
        if cpp_dir in ("buy", "sell") and sig_strength >= 0.7:
            reason_parts.append(f"模型置信度不足（{conf:.2f}<{min_conf:.2f}）")
            reason_parts.append(
                f"模型置信不足，采用C++高强度信号兜底（{sig_name}，强度{sig_strength:.2f}）"
            )
            return cpp_dir, round(sig_strength * 0.75, 4), reason_parts
        reason_parts.append(f"模型置信度不足降级hold（{conf:.2f}<{min_conf:.2f}）")
        if cpp_dir != "hold":
            reason_parts.append(f"C++信号参考方向{sig_name}")
        return "hold", round(conf, 4), reason_parts

    if model_decision == "hold":
        reason_parts.append(f"模型判断观望（hold概率{model_prob:.2f}）")
        if cpp_dir != "hold":
            reason_parts.append(f"C++信号{sig_name}仅作参考")
        return "hold", round(conf * model_prob, 4), reason_parts

    if cpp_dir == model_decision:
        bonus = 0.5 + 0.5 * sig_strength
        confidence = min(1.0, conf * model_prob + 0.15 * bonus)
        dir_text = "做多" if model_decision == "buy" else "做空"
        reason_parts.append(f"模型与C++信号同向{dir_text}")
        reason_parts.append(f"模型置信{conf:.2f}、C++信号强度{sig_strength:.2f}，置信度加成")
        return model_decision, round(confidence, 4), reason_parts

    if cpp_dir == "hold":
        reason_parts.append(f"C++无明确信号，采用模型决策{model_decision}")
        reason_parts.append(f"模型置信{conf:.2f}、类别概率{model_prob:.2f}")
        return model_decision, round(conf * model_prob, 4), reason_parts

    model_weight = conf * model_prob
    cpp_weight = sig_strength
    if model_weight >= cpp_weight:
        winner, winner_w = model_decision, model_weight
        reason_parts.append(f"模型与C++信号冲突，采信模型{model_decision}并降权")
    else:
        winner, winner_w = cpp_dir, cpp_weight
        reason_parts.append(f"模型与C++信号冲突，采信C++信号{sig_name}并降权")
    confidence = max(0.0, min(1.0, winner_w * 0.7))
    reason_parts.append(f"冲突降权后置信度{confidence:.2f}")
    return winner, round(confidence, 4), reason_parts


# LLM 置信度上限：代码说了算，大模型不允许给出接近满仓级的置信度
_LLM_MAX_CONFIDENCE = 0.95


def _baseline_fields(baseline) -> tuple[str, float]:
    """从 baseline 提取 (decision, confidence)，兼容元组 / AgentDecision / dict / 字符串。"""
    if baseline is None:
        return "hold", 0.0
    if isinstance(baseline, (tuple, list)) and len(baseline) >= 1:
        decision = str(baseline[0])
        conf = float(baseline[1]) if len(baseline) >= 2 else 0.0
        return decision, conf
    if isinstance(baseline, dict):
        return str(baseline.get("decision", "hold")), float(baseline.get("confidence", 0.0) or 0.0)
    if isinstance(baseline, str):
        return baseline, 0.0
    return (
        str(getattr(baseline, "final_decision", "hold")),
        float(getattr(baseline, "confidence", 0.0) or 0.0),
    )


def _risk_level(risk_info) -> tuple[str, str]:
    """从 risk_info 提取 (level, detail)，兼容 check_risk 结果 dict / 对象 / detail 字符串。"""
    if isinstance(risk_info, dict):
        return str(risk_info.get("level", "")).lower(), str(risk_info.get("detail", ""))
    if isinstance(risk_info, str):
        return "", risk_info
    return str(getattr(risk_info, "level", "")).lower(), str(getattr(risk_info, "detail", ""))


def validate_llm_decision(llm_decision, baseline, risk_info, agent_cfg):
    """代码级护栏：校验并修正 LLM 决策，规则优先级高于大模型（纯函数）。

    规则（按序执行）：
      1. llm_decision 为 None → 返回 None（调用方走模板兜底）；
      2. final_decision 非法（非 buy/sell/hold）→ 用 baseline 决策替换并标注；
      3. risk_info.level == 'high' 时 LLM 说 buy → 强制降级 hold（风控一票否决）；
      4. confidence 截断到 [0, 0.95]；
      5. LLM 与 baseline 决策冲突 → reason 追加「LLM反驳基线：…」标注。

    参数:
        llm_decision: AgentDecision / dict / None（chain.explain 的输出）。
        baseline: 确定性流水线的基线决策，(decision, confidence) 元组 / AgentDecision / dict / 字符串。
        risk_info: check_risk 结果 dict（{"level", "detail"}），兼容对象形态。
        agent_cfg: AgentConfig（预留阈值扩展）。
    返回:
        修正后的 AgentDecision（validate() 必通过），或 None。
    """
    if llm_decision is None:
        return None

    if isinstance(llm_decision, dict):
        llm_decision = AgentDecision.from_dict(llm_decision)

    base_decision, _ = _baseline_fields(baseline)
    if base_decision not in ("buy", "sell", "hold"):
        base_decision = "hold"

    decision = str(llm_decision.final_decision)
    annotations: list[str] = []

    if decision not in ("buy", "sell", "hold"):
        annotations.append(f"LLM决策{decision!r}非法，已按基线替换为{base_decision}")
        decision = base_decision

    level, risk_detail = _risk_level(risk_info)
    if level == "high" and decision == "buy":
        decision = "hold"
        annotations.append(f"风险等级high，风控一票否决降级hold（{risk_detail or '持仓深度亏损'}）")

    try:
        confidence = float(llm_decision.confidence)
    except (TypeError, ValueError):
        confidence = 0.0
    confidence = max(0.0, min(_LLM_MAX_CONFIDENCE, confidence))

    if decision != base_decision:
        annotations.append(f"LLM反驳基线：基线为{base_decision}，LLM给出{decision}")

    reason = str(llm_decision.reason or "").strip() or "LLM未给出理由"
    if annotations:
        reason = reason + "；" + "；".join(annotations)

    guarded = AgentDecision(
        symbol=str(llm_decision.symbol),
        timestamp=str(llm_decision.timestamp),
        final_decision=decision,
        confidence=round(confidence, 4),
        reason=reason,
    )
    if hasattr(guarded, "validate") and not guarded.validate():
        return None
    return guarded