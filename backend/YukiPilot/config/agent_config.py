"""
@file agent_config.py
@brief YukiPilot 配置层

层级：
    backend/YukiPilot/config/

项目内绝对路径：
    backend/YukiPilot/config/agent_config.py

模块作用：
    集中定义三类配置：
    - ModelConfig：Transformer 模型超参数；
    - TrainConfig：离线训练超参数；
    - AgentConfig：主循环运行参数。

使用者：
    YukiPilot/config/__init__.py 导出本模块三个配置类。
    YukiPilot/main.py 加载 AgentConfig 与 TrainConfig。
    YukiPilot/attention/model.py 加载 ModelConfig。

项目角色：
    配置层核心模块，所有可调参数在此集中管理并校验。

引入说明：
    依赖标准库 os，依赖 dataclasses 模块。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径与内部引用
"""

import os
from dataclasses import dataclass, field

# YukiPilot 包目录（即 main.py 所在的 backend/YukiPilot/）：
# 本文件位于 <包目录>/config/agent_config.py，上溯两级即包目录。
_PACKAGE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def resolve_package_path(path: str) -> str:
    """把相对路径锚定到 YukiPilot 包目录并规范化为绝对路径；绝对路径原样返回。

    背景：runtime_dir / checkpoint_path 等默认值是相对于「包目录」书写的
    （如 "../runtime_files" 恒定指向 backend/runtime_files），
    若直接交给 os 按当前工作目录（cwd）解析，则只有在 backend/YukiPilot/
    下直跑才碰巧正确，从 backend/ 或其他目录启动会静默读写错误位置。
    因此统一在这里锚定到包目录，与 cwd 无关；用户显式传入绝对路径时不做改动。
    """
    if not path:
        return path
    if os.path.isabs(path):
        return os.path.normpath(path)
    return os.path.normpath(os.path.join(_PACKAGE_DIR, path))


@dataclass
class ModelConfig:
    """FactorTransformer 模型超参数（特征顺序见 SPEC 3.3）。"""

    num_features: int = 10      # 输入特征数，见 3.3 特征顺序
    seq_len: int = 32           # 时序窗口长度
    d_model: int = 64
    nhead: int = 4
    num_layers: int = 2
    dim_feedforward: int = 128
    dropout: float = 0.1
    num_classes: int = 3        # 0=hold 1=buy 2=sell

    def validate(self) -> bool:
        """校验模型超参数；不合理时打印中文警告并返回 False。"""
        ok = True
        if self.num_features <= 0:
            print("[警告] ModelConfig.num_features 必须为正整数")
            ok = False
        if self.seq_len <= 0:
            print("[警告] ModelConfig.seq_len 必须为正整数")
            ok = False
        if self.d_model <= 0 or self.d_model % self.nhead != 0:
            # MultiheadAttention 要求 d_model 能被 nhead 整除
            print("[警告] ModelConfig.d_model 必须为正且能被 nhead 整除")
            ok = False
        if self.nhead <= 0 or self.num_layers <= 0 or self.dim_feedforward <= 0:
            print("[警告] ModelConfig.nhead/num_layers/dim_feedforward 必须为正整数")
            ok = False
        if not (0.0 <= self.dropout < 1.0):
            print("[警告] ModelConfig.dropout 必须落在 [0, 1) 区间")
            ok = False
        if self.num_classes != 3:
            # 决策空间固定为 hold/buy/sell 三类，改动会破坏跨语言契约
            print("[警告] ModelConfig.num_classes 固定为 3（0=hold 1=buy 2=sell）")
            ok = False
        return ok


@dataclass
class TrainConfig:
    """离线训练超参数（标签：未来 N 日收益，阈值定 buy/sell）。"""

    lr: float = 1e-3
    epochs: int = 20
    batch_size: int = 64
    forward_days: int = 5       # 标签：未来 N 日收益
    buy_threshold: float = 0.02   # 未来收益 > +2% → buy
    sell_threshold: float = -0.02 # 未来收益 < -2% → sell
    checkpoint_path: str = "checkpoints/factor_transformer.pt"
    stream_progress: bool = True  # True 时训练阶段按 batch 实时刷新 \r 进度条；False 退回纯 epoch 日志

    def __post_init__(self):
        # 相对路径锚定到包目录下的 checkpoints/，避免训练/加载 checkpoint
        # 随启动时的 cwd 变化而读写错位；绝对路径不做改动。
        self.checkpoint_path = resolve_package_path(self.checkpoint_path)

    def validate(self) -> bool:
        """校验训练超参数；不合理时打印中文警告并返回 False。"""
        ok = True
        if self.lr <= 0:
            print("[警告] TrainConfig.lr 必须为正数")
            ok = False
        if self.epochs <= 0 or self.batch_size <= 0 or self.forward_days <= 0:
            print("[警告] TrainConfig.epochs/batch_size/forward_days 必须为正整数")
            ok = False
        if self.sell_threshold >= self.buy_threshold:
            # 卖阈值必须严格小于买阈值，否则标签重叠无法训练
            print("[警告] TrainConfig.sell_threshold 必须小于 buy_threshold")
            ok = False
        if not self.checkpoint_path:
            print("[警告] TrainConfig.checkpoint_path 不能为空")
            ok = False
        return ok


@dataclass
class AgentConfig:
    """主循环运行参数（与 C++ 侧 PortfolioConfig 对齐的部分见注释）。"""

    runtime_dir: str = "../runtime_files"
    poll_interval: float = 2.0        # 秒
    min_confidence: float = 0.6       # 低于此置信度降级为 hold
    max_position_ratio: float = 0.2   # 与 C++ PortfolioConfig 对齐
    max_positions: int = 10
    use_llm: bool = False             # 无 API key 时强制 False
    llm_model: str = "gpt-4o-mini"
    agent_max_iterations: int = 6     # LLM Agent 工具调用循环的最大轮数
    llm_stream: bool = True           # True 时 LLM 最终回答走 .stream() 逐 token 输出；False 或非流式环境自动回退 .invoke()
    model: ModelConfig = field(default_factory=ModelConfig)

    def __post_init__(self):
        # runtime_dir 为相对路径时锚定到 YukiPilot 包目录（见 resolve_package_path），
        # 使 "../runtime_files" 恒定解析到 backend/runtime_files，与 cwd 无关；
        # 用户传入绝对路径时不做改动。
        self.runtime_dir = resolve_package_path(self.runtime_dir)

    def validate(self) -> bool:
        """校验运行参数（含内嵌 ModelConfig）；不合理时打印中文警告并返回 False。"""
        ok = True
        if not self.runtime_dir:
            print("[警告] AgentConfig.runtime_dir 不能为空")
            ok = False
        if self.poll_interval <= 0:
            print("[警告] AgentConfig.poll_interval 必须为正数（秒）")
            ok = False
        if not (0.0 <= self.min_confidence <= 1.0):
            print("[警告] AgentConfig.min_confidence 必须落在 [0, 1] 区间")
            ok = False
        if not (0.0 < self.max_position_ratio <= 1.0):
            print("[警告] AgentConfig.max_position_ratio 必须落在 (0, 1] 区间")
            ok = False
        if self.max_positions <= 0:
            print("[警告] AgentConfig.max_positions 必须为正整数")
            ok = False
        if self.use_llm and not self.llm_model:
            print("[警告] AgentConfig.use_llm=True 时 llm_model 不能为空")
            ok = False
        if self.agent_max_iterations <= 0:
            print("[警告] AgentConfig.agent_max_iterations 必须为正整数")
            ok = False
        if not self.model.validate():
            ok = False
        return ok