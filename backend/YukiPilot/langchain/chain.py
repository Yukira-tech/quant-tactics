"""
@file chain.py
@brief DecisionChain：tool-calling LLM Agent 决策复核 + 规则模板降级

层级：
    backend/YukiPilot/langchain/

项目内绝对路径：
    backend/YukiPilot/langchain/chain.py

模块作用：
    提供 DecisionChain 类，在 LLM 可用时以 tool-calling Agent 方式复核
    决策，不可用或异常时返回 None 让主流程降级为规则模板。

使用者：
    YukiPilot/main.py 导入 DecisionChain 构建决策链。

项目角色：
    LLM 决策层的核心实现，是 YukiPilot 三层智能架构中最上层的推理模块。

引入说明：
    依赖标准库 json、os、time、datetime。
    从 ..dataclass.schemas 导入 AgentDecision。
    从 .prompts 导入 AGENT_SYSTEM_PROMPT、AGENT_USER_TEMPLATE。
    从 .tools 导入 build_tools。
    可选依赖 langchain_openai 与 langchain_core，导入失败不影响模块加载。

维护记录：
    2026-08-29 初始创建，项目由 calcAgent 改名为 YukiPilot
"""

from __future__ import annotations

import json
import os
import time
from datetime import datetime

from ..dataclass.schemas import AgentDecision
from .prompts import AGENT_SYSTEM_PROMPT, AGENT_USER_TEMPLATE
from .tools import build_tools

# langchain-openai 可选导入：任何失败都视为不可用
try:
    from langchain_openai import ChatOpenAI
except Exception:  # noqa: BLE001
    ChatOpenAI = None

# langchain_core 消息类型可选导入（工具循环需要 ToolMessage，流式累积兜底需要 AIMessage）
try:
    from langchain_core.messages import (
        AIMessage,
        HumanMessage,
        SystemMessage,
        ToolMessage,
    )
except Exception:  # noqa: BLE001
    AIMessage = HumanMessage = SystemMessage = ToolMessage = None

# 合法决策枚举（与 JSON 契约一致）
_VALID_DECISIONS = {"buy", "sell", "hold"}

# LLM Agent 单次复核的墙钟时间预算（秒）：超时即放弃，走模板兜底
_LLM_TIMEOUT_S = 30.0


class DecisionChain:
    """LLM 决策链：available() 为 True 时以 tool-calling Agent 方式复核决策。

    参数:
        agent_cfg: AgentConfig，使用其 use_llm / llm_model / agent_max_iterations 字段。
    """

    def __init__(self, agent_cfg):
        self.cfg = agent_cfg
        self._llm = None
        # 仅当配置启用、API key 存在且 langchain-openai 可用时才初始化 LLM
        if getattr(agent_cfg, "use_llm", False) and os.environ.get("OPENAI_API_KEY"):
            if ChatOpenAI is not None:
                try:
                    self._llm = ChatOpenAI(
                        model=getattr(agent_cfg, "llm_model", "gpt-4o-mini"),
                        temperature=0,
                        timeout=_LLM_TIMEOUT_S,
                    )
                except Exception:  # noqa: BLE001 - 初始化失败按不可用处理
                    self._llm = None

    def available(self) -> bool:
        """LLM 链路是否可用（use_llm 且 langchain + API key 均就绪）。"""
        return self._llm is not None

    def explain(
        self,
        symbol,
        factor,
        signal,
        position,
        model_decision,
        model_conf,
        risk_info,
        *,
        reader=None,
        positions_count=0,
        reason_parts=None,
        on_token=None,
        thinking=None,
    ) -> "AgentDecision | None":
        """以 tool-calling Agent 方式复核并生成最终决策；不可用或失败时返回 None。

        参数:
            symbol: 标的代码。
            factor / signal / position: FactorOutput / SignalOutput / PositionOutput 对象或 dict，可为 None。
            model_decision: 融合后的基线决策 buy/sell/hold。
            model_conf: 融合后置信度。
            risk_info: 风险信息字符串（check_risk 的 detail）。
            reader: RuntimeReader（工具回退读取用，可为 None）。
            positions_count: 当前持仓标的数量（仓位约束工具用）。
            reason_parts: 基线决策的中文依据短语列表（get_model_baseline 工具用）。
            on_token: 可选流式回调，签名 on_token(text)。仅在「无 tool_call 的最终回答」
                阶段逐 token 调用；为 None 或 AgentConfig.llm_stream=False 时
                走原有 .invoke() 非流式路径，行为与升级前一致。
            thinking: 可选 ThinkingWriter（FE2 1.2）。传入后在工具调用前后写
                tool/tool_result 事件、流式 token 聚合节流写 text 事件；
                None 时零开销跳过，行为与升级前一致。
        返回:
            AgentDecision 或 None（由调用方降级为规则模板）。
        """
        if not self.available():
            return None
        try:
            return self._explain_with_agent(
                symbol=symbol,
                factor=factor,
                signal=signal,
                position=position,
                model_decision=model_decision,
                model_conf=model_conf,
                risk_info=risk_info,
                reader=reader,
                positions_count=positions_count,
                reason_parts=reason_parts,
                on_token=on_token,
                thinking=thinking,
            )
        except Exception:  # noqa: BLE001 - 任何 LLM/解析异常都不中断主循环
            return None

    def _explain_with_agent(
        self,
        symbol,
        factor,
        signal,
        position,
        model_decision,
        model_conf,
        risk_info,
        reader,
        positions_count,
        reason_parts,
        on_token=None,
        thinking=None,
    ) -> "AgentDecision | None":
        """手写 tool-calling 循环：本地执行工具 → ToolMessage 喂回 → 直到最终文本。"""
        # 消息类型缺失（langchain_core 未装）时直接放弃，走模板兜底
        if HumanMessage is None or SystemMessage is None or ToolMessage is None:
            return None

        # FE2 1.2 思考流埋点：开始复核状态事件
        if thinking is not None:
            thinking.add_status(f"开始复核 {symbol}")

        # 1. 构建绑定运行时上下文的工具集；为空说明 langchain_core 不可用
        tools = build_tools(
            {
                "reader": reader,
                "agent_cfg": self.cfg,
                "symbol": symbol,
                "factor": factor,
                "signal": signal,
                "position": position,
                "positions_count": positions_count,
                "baseline_decision": model_decision,
                "baseline_confidence": model_conf,
                "reason_parts": reason_parts,
            }
        )
        if not tools:
            return None
        tool_map = {t.name: t for t in tools}

        # 2. 绑定工具到 LLM；个别模型实现不支持 bind_tools 时降级为不绑定
        #    （工具仍在本地按 tool_calls 执行，FakeListChatModel 等测试替身即如此）
        llm = self._llm
        if hasattr(llm, "bind_tools"):
            try:
                llm = llm.bind_tools(tools)
            except Exception:  # noqa: BLE001 - 不支持绑定时按原样调用
                llm = self._llm

        # FE2 1.2 思考流桥接：聚合流式 token 成累积文本，节流到每 200ms 或每 20 token
        # 调一次 thinking.update_text（ThinkingWriter 内部只保留一条 text 事件原地更新）；
        # thinking=None 时 stream_cb 即原 on_token，零开销跳过。
        token_acc = {"text": "", "count": 0, "last": 0.0}
        stream_cb = on_token
        if thinking is not None:

            def stream_cb(text, _acc=token_acc, _cb=on_token, _thinking=thinking):
                if _cb is not None:
                    _cb(text)  # 终端打字机等外部回调原样透传
                _acc["text"] += text
                _acc["count"] += 1
                now = time.monotonic()
                if now - _acc["last"] >= 0.2 or _acc["count"] >= 20:
                    _thinking.update_text(_acc["text"])
                    _acc["last"] = now
                    _acc["count"] = 0

        # 3. Agent 循环：最多 agent_max_iterations 轮，墙钟预算 _LLM_TIMEOUT_S 秒
        max_iter = max(1, int(getattr(self.cfg, "agent_max_iterations", 6)))
        messages = [
            SystemMessage(content=AGENT_SYSTEM_PROMPT),
            HumanMessage(
                content=AGENT_USER_TEMPLATE.format(
                    symbol=symbol,
                    model_decision=model_decision,
                    model_confidence=f"{float(model_conf):.2f}",
                    risk_info=str(risk_info),
                )
            ),
        ]
        start = time.monotonic()
        resp = None
        for _ in range(max_iter):
            if time.monotonic() - start > _LLM_TIMEOUT_S:
                return None  # 超时：放弃 LLM 结果，走模板兜底
            # 流式优先：仅当外部传入 on_token 且 llm_stream=True 时启用 .stream()；
            # 返回 tool_calls 的响应 content 为空（工具调用阶段本来就不产生用户可见
            # token），因此对它流式与否用户无感知；真正逐 token 可见的是最终回答。
            resp = self._call_llm(llm, messages, stream_cb)
            messages.append(resp)
            tool_calls = getattr(resp, "tool_calls", None) or []
            if not tool_calls:
                break  # 无工具调用：已到最终回答
            for call in tool_calls:
                # tool_calls 为 dict 形态（{"name", "args", "id"}），兼容对象形态
                if isinstance(call, dict):
                    name, args, call_id = (
                        call.get("name", ""),
                        call.get("args") or {},
                        call.get("id"),
                    )
                else:
                    name, args, call_id = (
                        getattr(call, "name", ""),
                        getattr(call, "args", None) or {},
                        getattr(call, "id", None),
                    )
                # 工具调用阶段提示：LLM 不产出用户可见 token，打印一行说明正在本地执行哪个工具
                print(f"[LLM] 调用工具: {name}")
                if thinking is not None:
                    thinking.add_tool(name)  # FE2 1.2：工具调用前事件
                tool = tool_map.get(name)
                if tool is None:
                    result = json.dumps({"error": f"未知工具 {name!r}"}, ensure_ascii=False)
                else:
                    try:
                        result = str(tool.invoke(args))
                    except Exception as exc:  # noqa: BLE001 - 工具异常不中断循环
                        result = json.dumps({"error": f"工具 {name} 执行失败：{exc}"}, ensure_ascii=False)
                if thinking is not None:
                    # FE2 1.2：工具结果事件，text 取返回 JSON 的前 80 字符摘要
                    thinking.add_tool_result(name, result[:80])
                messages.append(ToolMessage(content=result, tool_call_id=call_id or name))
        else:
            # 循环跑满仍要求工具调用：模型失控，放弃 LLM 结果
            return None

        # 4. 解析最终文本为严格 JSON → AgentDecision（流式累积的文本与此前的
        #    invoke 结果走完全相同的解析 + 校验路径，行为零变化）
        content = self._content_to_text(getattr(resp, "content", resp))
        if thinking is not None:
            # FE2 1.2：冲刷节流残留，保证 text 事件内容 = 最终完整累积文本；
            # 非流式路径（on_token=None / llm_stream=False / 流式回退）时用完整 content 兜底
            final_text = token_acc["text"] or str(content)
            if final_text:
                thinking.update_text(final_text)
        data = self._parse_json(str(content))
        if data is None:
            return None
        decision = str(data.get("final_decision", "hold"))
        if decision not in _VALID_DECISIONS:
            return None
        try:
            confidence = max(0.0, min(1.0, float(data.get("confidence", 0.0))))
        except (TypeError, ValueError):
            return None
        reason = str(data.get("reason", "")).strip() or "LLM未给出理由"
        agent_decision = AgentDecision(
            symbol=str(symbol),
            timestamp=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            final_decision=decision,
            confidence=round(confidence, 4),
            reason=reason,
        )
        # 若 schema 提供 validate，校验失败同样降级
        if hasattr(agent_decision, "validate") and not agent_decision.validate():
            return None
        return agent_decision

    def _call_llm(self, llm, messages, on_token):
        """调用 LLM：启用流式时用 .stream() 逐 token 回调并累积完整消息，否则 .invoke()。

        流式细节：
        - 每个 chunk 取出可见文本立即调 on_token(text)（打字机效果）；
        - chunk 之间用 + 累积成完整 AIMessageChunk，tool_call_chunks 会被正确合并，
          因此流式拿到的 tool_calls 与 .invoke() 等价，工具循环不受影响；
        - 模型/环境不支持流式（Fake 模型、老版本、网络流式失败）时 try/except
          回退 .invoke()，功能等价、仅失去打字机效果。
        """
        stream_enabled = bool(getattr(self.cfg, "llm_stream", True))
        if on_token is None or not stream_enabled or not hasattr(llm, "stream"):
            return llm.invoke(messages)
        try:
            full = None        # 累积的完整消息 chunk（AIMessageChunk 支持 + 合并）
            text_parts = []    # 纯文本兜底：chunk 不支持相加时用文本重建
            for chunk in llm.stream(messages):
                if isinstance(chunk, str):  # 个别自定义模型直接 yield 字符串
                    text_parts.append(chunk)
                    if chunk:
                        on_token(chunk)
                    continue
                piece = self._content_to_text(getattr(chunk, "content", ""))
                if piece:
                    on_token(piece)
                try:
                    full = chunk if full is None else full + chunk
                except Exception:  # noqa: BLE001 - 不能相加时退化为纯文本累积
                    text_parts.append(piece)
            if full is not None:
                return full
            if text_parts and AIMessage is not None:
                return AIMessage(content="".join(text_parts))
        except Exception:  # noqa: BLE001 - 流式不可用：回退非流式，功能等价
            pass
        return llm.invoke(messages)

    @staticmethod
    def _content_to_text(content) -> str:
        """把消息 content（str / content block 列表 / 其他）统一转成纯文本。"""
        if isinstance(content, list):  # 兼容 content block 列表形态
            return "".join(
                block.get("text", "") if isinstance(block, dict) else str(block)
                for block in content
            )
        return str(content) if content is not None else ""

    def explain_template(self, symbol, final_decision, confidence, reason_parts) -> AgentDecision:
        """规则模板降级：用 ' + '.join(reason_parts) 拼接中文理由生成决策。

        参数:
            symbol: 标的代码。
            final_decision: 最终决策 buy/sell/hold。
            confidence: 置信度 [0, 1]。
            reason_parts: 中文短语列表（fuse_decision 等产出）。
        返回:
            合法的 AgentDecision。
        """
        reason = " + ".join(reason_parts) if reason_parts else "规则融合决策"
        return AgentDecision(
            symbol=str(symbol),
            timestamp=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            final_decision=str(final_decision),
            confidence=round(max(0.0, min(1.0, float(confidence))), 4),
            reason=reason,
        )

    @staticmethod
    def _parse_json(text: str) -> "dict | None":
        """从 LLM 输出中提取 JSON 对象，容忍 ```json 代码块包裹。"""
        text = text.strip()
        # 去掉 markdown 代码块围栏
        if text.startswith("```"):
            lines = text.splitlines()
            lines = [ln for ln in lines if not ln.strip().startswith("```")]
            text = "\n".join(lines).strip()
        # 截取第一个 { 到最后一个 }，容忍首尾多余文本
        start, end = text.find("{"), text.rfind("}")
        if start == -1 or end == -1 or end <= start:
            return None
        try:
            data = json.loads(text[start : end + 1])
        except Exception:  # noqa: BLE001
            return None
        return data if isinstance(data, dict) else None