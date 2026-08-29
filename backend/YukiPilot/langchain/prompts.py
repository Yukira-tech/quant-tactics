"""
@file prompts.py
@brief 决策解释用的中文 Prompt 模板

层级：
    backend/YukiPilot/langchain/

项目内绝对路径：
    backend/YukiPilot/langchain/prompts.py

模块作用：
    提供 LLM Agent 使用的系统提示词和用户消息模板。
    AGENT_SYSTEM_PROMPT / AGENT_USER_TEMPLATE 供 tool-calling LLM Agent 使用，
    DECISION_PROMPT 保留用于兼容早期单次调用形态。

使用者：
    YukiPilot/langchain/chain.py 导入 AGENT_SYSTEM_PROMPT 和 AGENT_USER_TEMPLATE。
    YukiPilot/main.py 可能导入 DECISION_PROMPT 兼容旧逻辑。

项目角色：
    决策提示词定义层，是 LLM 决策链的“工作手册”。

引入说明：
    依赖可选第三方 langchain 库。
    langchain 可用时 DECISION_PROMPT 为 PromptTemplate 实例，
    不可用时降级为同接口的纯字符串模板。

维护记录：
    2026-08-29 初始创建，项目由 calcAgent 改名为 YukiPilot
"""

# LLM Agent 系统提示词：角色 + 工具调用纪律 + 输出契约
AGENT_SYSTEM_PROMPT = """你是一名量化交易决策复核员，负责复核规则系统（Transformer 模型 + 规则融合流水线）给出的基线交易决策。

工作纪律：
1. 必须先调用 get_factor_snapshot 获取当前标的的因子/信号/持仓快照，并调用 check_risk 评估持仓风险；缺少事实依据不得下结论。
2. 可调用 check_signal_consistency 核对 C++ 信号与因子方向是否一致，调用 check_position_constraint 验证拟执行方向是否触发仓位约束，调用 get_model_baseline 查看规则系统的基线意见。
3. 基线决策可供参考，也可以反驳，但反驳必须在 reason 中给出明确理由。
4. confidence 取值 0 到 1，除非证据非常充分，不要给出极端置信度。
5. 决策只允许 buy / sell / hold 三种，禁止输出其他值。

收集完事实后，严格输出如下 JSON（不要输出任何其他内容）：
{"final_decision": "buy 或 sell 或 hold", "confidence": 0到1之间的小数, "reason": "中文决策理由，简短说明依据"}
"""

# LLM Agent 用户消息模板：告知标的与基线决策，引导先调工具再下结论
AGENT_USER_TEMPLATE = """请复核标的 {symbol} 的交易决策。
规则系统基线决策：{model_decision}（置信度 {model_confidence}）；已知风险信息：{risk_info}。
请先调用 get_factor_snapshot 与 check_risk 等工具获取事实，再给出最终 JSON 决策。"""

# 模板正文：要求 LLM 严格输出 JSON（final_decision/confidence/reason）
_TEMPLATE = """你是一名量化交易决策助手。请根据以下信息给出最终交易决策。

标的代码：{symbol}
因子数据：{factor_json}
C++信号：{signal_json}
当前持仓：{position_json}
融合模型初步决策：{model_decision}（置信度 {model_confidence}）
风险信息：{risk_info}

请综合判断，并严格输出如下 JSON（不要输出任何其他内容）：
{{
  "final_decision": "buy 或 sell 或 hold",
  "confidence": 0到1之间的小数,
  "reason": "中文决策理由，简短说明依据"
}}
"""

try:
    # 优先使用真正的 langchain PromptTemplate
    from langchain.prompts import PromptTemplate

    DECISION_PROMPT = PromptTemplate(
        template=_TEMPLATE,
        input_variables=[
            "symbol",
            "factor_json",
            "signal_json",
            "position_json",
            "model_decision",
            "model_confidence",
            "risk_info",
        ],
    )
except Exception:  # noqa: BLE001 - langchain 缺失或被本地同名目录遮蔽时兜底
    class _PlainTemplate:
        """无 langchain 环境的兜底模板，仅保留 .format(**kwargs) 接口。"""

        def __init__(self, template: str):
            self.template = template

        def format(self, **kwargs) -> str:
            return self.template.format(**kwargs)

    DECISION_PROMPT = _PlainTemplate(_TEMPLATE)