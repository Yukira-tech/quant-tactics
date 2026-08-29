"""
@file components.py
@brief 注意力机制基础组件

层级：
    backend/YukiPilot/attention/

项目内绝对路径：
    backend/YukiPilot/attention/components.py

模块作用：
    提供正弦位置编码 PositionalEncoding 和 pre-norm Transformer 编码块
    TransformerEncoderBlock，供 FactorTransformer 组装使用。

使用者：
    YukiPilot/attention/model.py 导入本模块组件构建模型。

项目角色：
    模型层的基础组件，承担时间步位置编码和编码器堆叠的核心结构。

引入说明：
    依赖标准库 math，依赖 torch 和 torch.nn。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot，同步更新路径
"""

import math

import torch
import torch.nn as nn


class PositionalEncoding(nn.Module):
    """标准正弦位置编码（sinusoidal positional encoding）。

    输入 x: [B, T, d_model]，输出与 x 形状相同（叠加位置信息后过 dropout）。
    """

    def __init__(self, d_model: int, max_len: int = 512, dropout: float = 0.1):
        super().__init__()
        self.dropout = nn.Dropout(p=dropout)

        # 预先计算 [max_len, d_model] 的位置编码表，注册为 buffer（不参与训练、随模型保存）
        pe = torch.zeros(max_len, d_model)
        position = torch.arange(0, max_len, dtype=torch.float32).unsqueeze(1)  # [max_len, 1]
        div_term = torch.exp(
            torch.arange(0, d_model, 2, dtype=torch.float32) * (-math.log(10000.0) / d_model)
        )
        pe[:, 0::2] = torch.sin(position * div_term)  # 偶数维用 sin
        pe[:, 1::2] = torch.cos(position * div_term)  # 奇数维用 cos
        self.register_buffer("pe", pe.unsqueeze(0))  # [1, max_len, d_model]

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """x: [B, T, d_model] -> [B, T, d_model]，叠加前 T 个位置的位置编码。"""
        seq_len = x.size(1)
        if seq_len > self.pe.size(1):
            raise ValueError(
                f"序列长度 {seq_len} 超过位置编码最大长度 {self.pe.size(1)}，请增大 max_len"
            )
        x = x + self.pe[:, :seq_len]
        return self.dropout(x)


class TransformerEncoderBlock(nn.Module):
    """pre-norm 结构的 Transformer 编码块。

    结构：x = x + MultiheadAttention(LN(x))；x = x + FFN(LN(x))，共两个残差连接。
    pre-norm 相比 post-norm 训练更稳定，梯度可以直接走残差通路。
    """

    def __init__(self, d_model: int, nhead: int, dim_feedforward: int, dropout: float = 0.1):
        super().__init__()
        self.norm1 = nn.LayerNorm(d_model)
        self.attn = nn.MultiheadAttention(
            embed_dim=d_model, num_heads=nhead, dropout=dropout, batch_first=True
        )
        self.dropout1 = nn.Dropout(dropout)

        self.norm2 = nn.LayerNorm(d_model)
        self.ffn = nn.Sequential(
            nn.Linear(d_model, dim_feedforward),
            nn.GELU(),
            nn.Dropout(dropout),
            nn.Linear(dim_feedforward, d_model),
        )
        self.dropout2 = nn.Dropout(dropout)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """x: [B, T, d_model] -> [B, T, d_model]。"""
        # 自注意力子层（pre-norm + 残差）
        h = self.norm1(x)
        attn_out, _ = self.attn(h, h, h, need_weights=False)
        x = x + self.dropout1(attn_out)
        # 前馈子层（pre-norm + 残差）
        x = x + self.dropout2(self.ffn(self.norm2(x)))
        return x