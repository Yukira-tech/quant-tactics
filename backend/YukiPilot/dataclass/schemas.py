"""
@file schemas.py
@brief 数据契约定义

层级：
    backend/YukiPilot/dataclass/

项目内绝对路径：
    backend/YukiPilot/dataclass/schemas.py

模块作用：
    定义 Python 侧与 C++/Go 侧交换的数据契约类。
    Kline、FactorOutput、SignalOutput、PositionOutput 是 C++ 写给我们的输入；
    AgentDecision 是我们写回给 C++ 的输出。
    每个类都是 @dataclass，Python 属性名 = JSON 字段名；
    from_dict() 容忍缺字段，to_dict() 原样导出。

使用者：
    YukiPilot/dataclass/io.py 导入本模块的数据类进行读写。
    YukiPilot/skill/tools.py 导入本模块的数据类进行决策融合。

项目角色：
    数据契约层核心，是 YukiPilot 与 C++/Go 之间数据交换的“单据”定义。

引入说明：
    依赖标准库 dataclasses 和 typing。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""

from dataclasses import asdict, dataclass
from typing import Any


@dataclass
class Kline:
    """单根 K 线（klines/{code}.jsonl 每行一条，按 ymd 升序）。"""

    code: int = 0
    ymd: int = 0
    open: float = 0.0
    high: float = 0.0
    low: float = 0.0
    close: float = 0.0
    volume: int = 0

    @classmethod
    def from_dict(cls, d: dict) -> "Kline":
        """从 dict 构造；缺字段用默认值，容忍脏数据。"""
        return cls(
            code=int(d.get("code", 0)),
            ymd=int(d.get("ymd", 0)),
            open=float(d.get("open", 0.0)),
            high=float(d.get("high", 0.0)),
            low=float(d.get("low", 0.0)),
            close=float(d.get("close", 0.0)),
            volume=int(d.get("volume", 0)),
        )

    def to_dict(self) -> dict:
        """导出为 dict，字段名与 SPEC 2.1 完全一致。"""
        return asdict(self)


@dataclass
class FactorOutput:
    """C++ FactorEngine 输出的因子快照（factor_outputs/{code}.json）。"""

    symbol: str = ""
    timestamp: str = ""
    ma_short: float = 0.0
    ma_long: float = 0.0
    donchian_high: float = 0.0
    donchian_low: float = 0.0
    atr: float = 0.0

    @classmethod
    def from_dict(cls, d: dict) -> "FactorOutput":
        """从 dict 构造；缺字段用默认值，容忍脏数据。"""
        return cls(
            symbol=str(d.get("symbol", "")),
            timestamp=str(d.get("timestamp", "")),
            ma_short=float(d.get("ma_short", 0.0)),
            ma_long=float(d.get("ma_long", 0.0)),
            donchian_high=float(d.get("donchian_high", 0.0)),
            donchian_low=float(d.get("donchian_low", 0.0)),
            atr=float(d.get("atr", 0.0)),
        )

    def to_dict(self) -> dict:
        """导出为 dict，字段名与 SPEC 2.1 完全一致。"""
        return asdict(self)


@dataclass
class SignalOutput:
    """C++ StrategyEngine 输出的策略信号（signals/{code}.json）。

    signal 取值：buy | sell | hold | short | cover。
    """

    symbol: str = ""
    timestamp: str = ""
    signal: str = "hold"
    strength: float = 0.0
    strategy_source: str = ""

    @classmethod
    def from_dict(cls, d: dict) -> "SignalOutput":
        """从 dict 构造；缺字段用默认值，容忍脏数据。"""
        return cls(
            symbol=str(d.get("symbol", "")),
            timestamp=str(d.get("timestamp", "")),
            signal=str(d.get("signal", "hold")),
            strength=float(d.get("strength", 0.0)),
            strategy_source=str(d.get("strategy_source", "")),
        )

    def to_dict(self) -> dict:
        """导出为 dict，字段名与 SPEC 2.1 完全一致。"""
        return asdict(self)

    def is_buy(self) -> bool:
        """是否偏多方向：buy 开多，cover 平空也属买入侧动作。"""
        return self.signal in ("buy", "cover")

    def is_sell(self) -> bool:
        """是否偏空方向：sell 平多/卖出，short 开空也属卖出侧动作。"""
        return self.signal in ("sell", "short")


@dataclass
class PositionOutput:
    """当前持仓快照（positions/{code}.json）。"""

    symbol: str = ""
    timestamp: str = ""
    quantity: int = 0
    avg_cost: float = 0.0
    current_price: float = 0.0
    floating_pnl: float = 0.0
    inventory_available: int = 0
    inventory_frozen: int = 0

    @classmethod
    def from_dict(cls, d: dict) -> "PositionOutput":
        """从 dict 构造；缺字段用默认值，容忍脏数据。"""
        return cls(
            symbol=str(d.get("symbol", "")),
            timestamp=str(d.get("timestamp", "")),
            quantity=int(d.get("quantity", 0)),
            avg_cost=float(d.get("avg_cost", 0.0)),
            current_price=float(d.get("current_price", 0.0)),
            floating_pnl=float(d.get("floating_pnl", 0.0)),
            inventory_available=int(d.get("inventory_available", 0)),
            inventory_frozen=int(d.get("inventory_frozen", 0)),
        )

    def to_dict(self) -> dict:
        """导出为 dict，字段名与 SPEC 2.1 完全一致。"""
        return asdict(self)


@dataclass
class AgentDecision:
    """Agent 最终决策（agent_decisions/{code}.json，SPEC 2.2）。

    final_decision 只输出 buy | sell | hold 三种。
    """

    symbol: str = ""
    timestamp: str = ""
    final_decision: str = "hold"
    confidence: float = 0.0
    reason: str = ""

    @classmethod
    def from_dict(cls, d: dict) -> "AgentDecision":
        """从 dict 构造；缺字段用默认值，容忍脏数据。"""
        return cls(
            symbol=str(d.get("symbol", "")),
            timestamp=str(d.get("timestamp", "")),
            final_decision=str(d.get("final_decision", "hold")),
            confidence=float(d.get("confidence", 0.0)),
            reason=str(d.get("reason", "")),
        )

    def to_dict(self) -> dict:
        """导出为 dict，字段名与 SPEC 2.2 完全一致。"""
        return asdict(self)

    def validate(self) -> bool:
        """校验决策合法性：decision ∈ {buy,sell,hold} 且 confidence ∈ [0,1]。"""
        ok = True
        if self.final_decision not in ("buy", "sell", "hold"):
            print(f"[警告] AgentDecision.final_decision 非法：{self.final_decision!r}（只允许 buy/sell/hold）")
            ok = False
        if not (0.0 <= self.confidence <= 1.0):
            print(f"[警告] AgentDecision.confidence 越界：{self.confidence}（必须落在 [0, 1]）")
            ok = False
        return ok