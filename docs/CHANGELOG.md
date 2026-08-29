# CHANGELOG.md

## 2026-08-27

- 从 `FileGuard.hpp` 开始，完成 C++ 文件句柄 RAII 封装
- 拆分 `FileSystem.hpp`、`RuntimeFileManager.hpp`
- 完成 `Kline.hpp` 64 字节缓存行对齐
- 完成 `ThreadPool.hpp`，支持移动语义
- 完成 `DataLoader.hpp`、`DataWriter.hpp`
- 建立 `runtime_files/` 跨语言文件通信架构

## 2026-08-28

- 完成 C++ 策略引擎核心：因子、信号、持仓、组合
- 引入 Python `calcAgent` 决策增强层
- 完成 Go 服务器：HTTP、WebSocket、文件监听
- 完成前端第一版终端风监控面板
- 建立 `bridge.hpp` 跨语言 JSON 契约

## 2026-08-29

- `calcAgent` 改名为 `YukiPilot`
- 实现三层智能降级架构：C++ 规则 → Transformer → LLM Agent
- 实现代码级安全护栏、LLM tool-calling 复核
- 前端改版为二次元贴纸风，四页 SPA
- 增加训练进度与 Agent 思考流可视化
- 完成 C++ 双入口：`main_single.cpp` / `main_modular.cpp`
- 统一全项目注释规范
- 补齐 ARCHITECTURE、TIMING、FEATURES、SPEC 文档体系
