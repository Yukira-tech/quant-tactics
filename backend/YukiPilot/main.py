"""
@file main.py
@brief YukiPilot 主循环入口

层级：
    backend/YukiPilot/

项目内绝对路径：
    backend/YukiPilot/main.py

模块作用：
    从 runtime_files/ 读取 C++ 产出的因子/信号/持仓/K线，
    用 FactorTransformer 模型 + 规则融合生成最终决策，原子写回 agent_decisions/。

使用者：
    作为模块运行入口，供命令行调用。

项目角色：
    YukiPilot 的启动与主循环模块，负责串联训练、推理、决策、写回全流程。

引入说明：
    依赖标准库 argparse、glob、os、sys、time、numpy、torch。
    从 .config 导入 AgentConfig、TrainConfig。
    从 .config.agent_config 导入 resolve_package_path。
    从 .dataclass 导入 DecisionWriter、RuntimeReader、ThinkingWriter、TrainingProgressWriter。
    从 .attention 导入 FactorTransformer。
    从 .skill.tools 导入决策技能。
    从 .langchain 导入 DecisionChain。
    从 .train.dataset 导入 build_feature_matrix。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径与回退导入
"""
from __future__ import annotations

import argparse
import glob
import os
import sys
import time

import numpy as np
import torch

try:
    # 包内相对导入（python -m YukiPilot.main 的正常路径）
    from .config import AgentConfig, TrainConfig
    from .config.agent_config import resolve_package_path
    from .dataclass import (
        DecisionWriter,
        RuntimeReader,
        ThinkingWriter,
        TrainingProgressWriter,
    )
    from .attention import FactorTransformer
    from .skill.tools import (
        fuse_decision,
        check_risk,
        check_position_constraint,
        validate_llm_decision,
    )
    from .langchain import DecisionChain
    from .train.dataset import build_feature_matrix
except ImportError:
    # 回退路径：在 YukiPilot/ 目录内直接 `python main.py` 时，
    # __package__ 为空导致相对导入失败，这里把父目录（backend/）加入 sys.path，
    # 再按 YukiPilot 包做绝对导入，行为与 python -m YukiPilot.main 完全一致。
    _PARENT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if _PARENT not in sys.path:
        sys.path.insert(0, _PARENT)
    from YukiPilot.config import AgentConfig, TrainConfig
    from YukiPilot.config.agent_config import resolve_package_path
    from YukiPilot.dataclass import (
        DecisionWriter,
        RuntimeReader,
        ThinkingWriter,
        TrainingProgressWriter,
    )
    from YukiPilot.attention import FactorTransformer
    from YukiPilot.skill.tools import (
        fuse_decision,
        check_risk,
        check_position_constraint,
        validate_llm_decision,
    )
    from YukiPilot.langchain import DecisionChain
    from YukiPilot.train.dataset import build_feature_matrix


def _count_positions(agent_cfg) -> int:
    """统计当前持仓标的数量：直接数 positions/ 下的 *.json 文件数（目录不存在时为 0）。"""
    pos_dir = os.path.join(agent_cfg.runtime_dir, "positions")
    try:
        return len(glob.glob(os.path.join(pos_dir, "*.json")))
    except Exception:  # noqa: BLE001 - 目录异常按 0 处理，不中断主循环
        return 0


class _TokenPrinter:
    """LLM 流式回答的打字机打印回调（终端 ANSI 控制，注释见下）。

    - 流式开始（首个 token）时先输出暗色前缀 \033[2m，让 LLM 原文与程序日志视觉区分；
    - 每个 token 直接写 stdout 并 flush，形成打字机效果；
    - finish() 在流式结束后输出 \033[0m 复位颜色并换行，保证后续日志格式不受影响；
    - enabled=False（llm_stream=False）时不产出任何输出，行为与升级前一致。
    """

    def __init__(self, enabled: bool = True):
        self.enabled = enabled
        self.started = False

    def __call__(self, text: str):
        if not self.enabled or not text:
            return
        if not self.started:
            sys.stdout.write("\033[2m")
            self.started = True
        sys.stdout.write(text)
        sys.stdout.flush()

    def finish(self):
        """流式结束：复位 ANSI 颜色并换行；未开始过则什么都不做。"""
        if self.started:
            sys.stdout.write("\033[0m\n")
            sys.stdout.flush()
            self.started = False


def run_once(agent_cfg, reader, writer, model, chain) -> list:
    """对所有标的执行一轮「读取 → 模型推理 → 融合 → 风险/约束检查 → 解释 → 写回」。

    参数:
        agent_cfg: AgentConfig。
        reader: RuntimeReader。
        writer: DecisionWriter。
        model: FactorTransformer（提供 predict(x) -> (probs, confidence)）。
        chain: DecisionChain（LLM 不可用时自动降级规则模板）。
    返回:
        本轮成功写出的 AgentDecision 列表。单个标的失败只打 warning，不中断本轮。
    """
    decisions = []
    seq_len = int(agent_cfg.model.seq_len)
    positions_count = _count_positions(agent_cfg)

    thinking_writer = ThinkingWriter(agent_cfg.runtime_dir) if chain.available() else None

    for code in reader.list_codes():
        try:
            klines = reader.read_klines(code, limit=seq_len + 40)
            if len(klines) < seq_len:
                print(f"[警告] {code} K线不足（{len(klines)}/{seq_len}），跳过本轮")
                continue
            feats = build_feature_matrix(klines)
            window = np.asarray(feats[-seq_len:], dtype=np.float32)
            x = torch.from_numpy(window).unsqueeze(0)

            probs, conf = model.predict(x)
            cpp_signal = reader.read_signal(code)
            decision, confidence, reason_parts = fuse_decision(
                cpp_signal, probs, conf, agent_cfg
            )

            position = reader.read_position(code)
            risk = check_risk(position, agent_cfg)
            if risk["level"] == "high" and decision != "hold":
                decision = "hold"
                confidence = round(confidence * 0.5, 4)
                reason_parts.append(f"风险过高降级hold（{risk['detail']}）")
            constraint = check_position_constraint(
                code, decision, positions_count, agent_cfg
            )
            if not constraint["allowed"] and decision == "buy":
                decision = "hold"
                reason_parts.append(f"仓位约束降级hold（{constraint['detail']}）")

            agent_decision = None
            if chain.available():
                token_printer = _TokenPrinter(
                    enabled=bool(getattr(agent_cfg, "llm_stream", True))
                )
                if thinking_writer is not None:
                    thinking_writer.start(code)
                llm_decision = chain.explain(
                    symbol=code,
                    factor=reader.read_factor(code),
                    signal=cpp_signal,
                    position=position,
                    model_decision=decision,
                    model_conf=confidence,
                    risk_info=risk["detail"],
                    reader=reader,
                    positions_count=positions_count,
                    reason_parts=list(reason_parts),
                    on_token=token_printer,
                    thinking=thinking_writer,
                )
                token_printer.finish()
                agent_decision = validate_llm_decision(
                    llm_decision, (decision, confidence), risk, agent_cfg
                )
            if agent_decision is None:
                agent_decision = chain.explain_template(
                    code, decision, confidence, reason_parts
                )
            if thinking_writer is not None:
                thinking_writer.finish(agent_decision.to_dict())

            path = writer.write(agent_decision)
            decisions.append(agent_decision)
            print(
                f"[决策] {code}: {agent_decision.final_decision} "
                f"(置信度 {agent_decision.confidence:.2f}) -> {path} | {agent_decision.reason}"
            )
        except Exception as exc:  # noqa: BLE001 - 单标的失败不影响其他标的
            print(f"[警告] {code} 本轮处理失败：{exc}")
            continue
    return decisions


def _build_model(agent_cfg, train_cfg) -> FactorTransformer:
    """构建模型并加载 checkpoint；checkpoint 缺失/损坏时警告并用随机初始化兜底，绝不崩溃。"""
    model = FactorTransformer(agent_cfg.model)
    ckpt_path = train_cfg.checkpoint_path
    if os.path.exists(ckpt_path):
        try:
            state = torch.load(ckpt_path, map_location="cpu")
            if isinstance(state, dict) and "model_state_dict" in state:
                state = state["model_state_dict"]
            model.load_state_dict(state)
            print(f"[信息] 已加载模型 checkpoint：{ckpt_path}")
        except Exception as exc:  # noqa: BLE001
            print(f"[警告] checkpoint 加载失败（{exc}），使用随机初始化模型兜底")
    else:
        print(f"[警告] checkpoint 不存在（{ckpt_path}），使用随机初始化模型兜底，决策由 C++ 信号保底")
    model.eval()
    return model


def main():
    """主入口：解析参数 → 构建组件 → （可选训练）→ 单次运行或轮询循环。"""
    parser = argparse.ArgumentParser(description="YukiPilot 决策增强层主循环")
    parser.add_argument("--once", action="store_true", help="只运行一轮后退出（冒烟测试用）")
    parser.add_argument("--train", action="store_true", help="先训练模型再进入轮询循环")
    parser.add_argument("--runtime-dir", default=None, help="覆盖 runtime_files 目录路径")
    args = parser.parse_args()

    agent_cfg = AgentConfig()
    if args.runtime_dir:
        # 命令行覆盖的相对路径同样锚定到 YukiPilot 包目录，绝对路径原样使用
        agent_cfg.runtime_dir = resolve_package_path(args.runtime_dir)
    train_cfg = TrainConfig()

    reader = RuntimeReader(agent_cfg.runtime_dir)
    writer = DecisionWriter(agent_cfg.runtime_dir)
    model = _build_model(agent_cfg, train_cfg)

    if args.train:
        try:
            from .train import build_dataset, Trainer
        except ImportError:
            from YukiPilot.train import build_dataset, Trainer

        print("[信息] 开始训练 FactorTransformer ...")
        X, y = build_dataset(agent_cfg.runtime_dir, agent_cfg.model, train_cfg)
        trainer = Trainer(agent_cfg.model, train_cfg)
        if len(X) > 0:
            progress_writer = TrainingProgressWriter(agent_cfg.runtime_dir)
            metrics = trainer.train(
                X, y,
                on_train_start=progress_writer.start,
                on_batch_end=progress_writer.on_batch_end,
                on_epoch_end=progress_writer.on_epoch_end,
            )
            progress_writer.finish()
        else:
            metrics = trainer.train(X, y)
        print(f"[信息] 训练完成：{metrics}")
        model = _build_model(agent_cfg, train_cfg)

    chain = DecisionChain(agent_cfg)
    print(f"[信息] LLM 决策链可用：{chain.available()}；runtime_dir={agent_cfg.runtime_dir}")

    if args.once:
        decisions = run_once(agent_cfg, reader, writer, model, chain)
        print(f"[信息] 单次运行完成，共产生 {len(decisions)} 条决策")
        return

    print(f"[信息] 进入轮询循环，间隔 {agent_cfg.poll_interval} 秒（Ctrl+C 退出）")
    try:
        while True:
            run_once(agent_cfg, reader, writer, model, chain)
            time.sleep(agent_cfg.poll_interval)
    except KeyboardInterrupt:
        print("\n[信息] 收到 Ctrl+C，优雅退出")


if __name__ == "__main__":
    main()