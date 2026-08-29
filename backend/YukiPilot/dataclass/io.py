"""
@file io.py
@brief 运行时文件读写

层级：
    backend/YukiPilot/dataclass/

项目内绝对路径：
    backend/YukiPilot/dataclass/io.py

模块作用：
    与 C++/Go 侧交接运行时文件的唯一入口。
    RuntimeReader 读取 factor_outputs、signals、positions、klines；
    DecisionWriter 将最终决策原子写回 agent_decisions。

使用者：
    YukiPilot/main.py 依赖本模块读取输入和写回决策。
    YukiPilot/skill/tools.py 通过 RuntimeReader 获取因子、信号、持仓。

项目角色：
    YukiPilot 与 runtime_files 之间的数据交换层。

引入说明：
    依赖标准库 json、os，依赖 .schemas 中的数据契约类。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""

import json
import os

from .schemas import AgentDecision, FactorOutput, Kline, PositionOutput, SignalOutput


class RuntimeReader:
    """读取 runtime_files/ 下 C++/Go 侧产出的各类 JSON 输入文件。"""

    def __init__(self, runtime_dir: str):
        """runtime_dir：runtime_files 目录路径（如 ../runtime_files）。"""
        self.runtime_dir = runtime_dir

    def list_codes(self) -> list[str]:
        """扫描 klines/ 下 *.jsonl，返回 code 列表（文件名去掉后缀）。"""
        klines_dir = os.path.join(self.runtime_dir, "klines")
        if not os.path.isdir(klines_dir):
            print(f"[警告] klines 目录不存在：{klines_dir}")
            return []
        try:
            codes = [
                name[: -len(".jsonl")]
                for name in os.listdir(klines_dir)
                if name.endswith(".jsonl")
            ]
            return sorted(codes)
        except OSError as e:
            print(f"[警告] 扫描 klines 目录失败：{e}")
            return []

    def _read_json(self, path: str) -> dict | None:
        """读取单个 JSON 文件；失败只警告返回 None，不抛异常。"""
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            if not isinstance(data, dict):
                print(f"[警告] JSON 顶层不是对象：{path}")
                return None
            return data
        except FileNotFoundError:
            # 文件暂未生成属正常情况（C++ 还没写），不打警告刷屏
            return None
        except (json.JSONDecodeError, OSError, UnicodeDecodeError) as e:
            print(f"[警告] 读取 JSON 失败 {path}：{e}")
            return None

    def read_factor(self, code: str) -> FactorOutput | None:
        """读 factor_outputs/{code}.json；不存在或损坏返回 None。"""
        data = self._read_json(os.path.join(self.runtime_dir, "factor_outputs", f"{code}.json"))
        if data is None:
            return None
        try:
            return FactorOutput.from_dict(data)
        except (TypeError, ValueError) as e:
            print(f"[警告] 解析因子数据失败 code={code}：{e}")
            return None

    def read_signal(self, code: str) -> SignalOutput | None:
        """读 signals/{code}.json；不存在或损坏返回 None。"""
        data = self._read_json(os.path.join(self.runtime_dir, "signals", f"{code}.json"))
        if data is None:
            return None
        try:
            return SignalOutput.from_dict(data)
        except (TypeError, ValueError) as e:
            print(f"[警告] 解析信号数据失败 code={code}：{e}")
            return None

    def read_position(self, code: str) -> PositionOutput | None:
        """读 positions/{code}.json；不存在或损坏返回 None。"""
        data = self._read_json(os.path.join(self.runtime_dir, "positions", f"{code}.json"))
        if data is None:
            return None
        try:
            return PositionOutput.from_dict(data)
        except (TypeError, ValueError) as e:
            print(f"[警告] 解析持仓数据失败 code={code}：{e}")
            return None

    def read_klines(self, code: str, limit: int | None = None) -> list[Kline]:
        """读 klines/{code}.jsonl（每行一条 JSON），按 ymd 升序返回；

        limit 取末尾 N 条。文件不存在或整文件损坏时返回空列表；
        单行损坏只跳过该行并警告，不影响其他行。
        """
        path = os.path.join(self.runtime_dir, "klines", f"{code}.jsonl")
        if not os.path.isfile(path):
            return []
        klines: list[Kline] = []
        try:
            with open(path, "r", encoding="utf-8") as f:
                for lineno, line in enumerate(f, 1):
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        klines.append(Kline.from_dict(json.loads(line)))
                    except (json.JSONDecodeError, TypeError, ValueError) as e:
                        print(f"[警告] K线第 {lineno} 行损坏已跳过 {path}：{e}")
        except OSError as e:
            print(f"[警告] 读取 K 线文件失败 {path}：{e}")
            return []
        klines.sort(key=lambda k: k.ymd)  # 按 ymd 升序
        if limit is not None:
            klines = klines[-limit:] if limit > 0 else []
        return klines


class DecisionWriter:
    """把 AgentDecision 原子写回 runtime_files/agent_decisions/。"""

    def __init__(self, runtime_dir: str):
        """runtime_dir：runtime_files 目录路径（如 ../runtime_files）。"""
        self.runtime_dir = runtime_dir

    def write(self, decision: AgentDecision) -> str:
        """原子写：先写 agent_decisions/{symbol}.json.tmp，再 os.replace 为 .json。

        返回最终文件路径；写入失败打印中文警告并返回空字符串，不抛异常。
        """
        out_dir = os.path.join(self.runtime_dir, "agent_decisions")
        try:
            os.makedirs(out_dir, exist_ok=True)
            final_path = os.path.join(out_dir, f"{decision.symbol}.json")
            tmp_path = final_path + ".tmp"
            with open(tmp_path, "w", encoding="utf-8") as f:
                json.dump(decision.to_dict(), f, ensure_ascii=False, indent=2)
                f.flush()
                os.fsync(f.fileno())  # 落盘后再替换，防止断电留下半截文件
            os.replace(tmp_path, final_path)
            return final_path
        except OSError as e:
            print(f"[警告] 写入决策失败 symbol={decision.symbol}：{e}")
            return ""