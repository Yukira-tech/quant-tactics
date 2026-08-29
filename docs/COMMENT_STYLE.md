# StrategyEngine 注释规范

本规范用于 StrategyEngine 项目内所有代码文件的注释维护。

适用范围：C++、Python、Go、JavaScript、HTML、CSS。  
SVG、go.mod、requirements.txt 等配置与素材文件不适用本规范。

核心目标：任何一个模块被重新打开时，不需要翻代码逻辑，只看文件头注释就能知道它是什么、属于哪一层、它在这个项目里干什么。

---

## 一、注释总原则

### 1. 注释先讲「模块」，再讲「代码」

每个文件、每个类、每个包，都要先回答这些问题：

1. 这个模块是什么？
2. 它位于项目哪一层？
3. 它在项目内的绝对路径是什么？
4. 它给谁用？
5. 它在整个项目里扮演什么角色？
6. 它引入了什么东西？
7. 它是什么时候创建、什么时候改过的？

代码内部再用 `//` 或 `#` 补足局部逻辑说明。

### 2. 注释是给人看的，不是给编译器看的

写注释时先想：半年后我自己回来看，第一眼需要知道什么？  
不是每行都写，而是每个「门面」都要写清楚。

### 3. 注释要可维护

每次修改模块功能，都要同步更新文件头注释。  
层级、路径、角色变化很重要，避免"注释说 A，代码做 B"。

### 4. 文件头永远在文件最顶部

文件头注释必须放在所有代码之前，包括：

- C++ 的 `#pragma once` 和 `#include`
- Python 的 `import`
- Go 的 `package`
- JavaScript 的 `(function () {`
- CSS 的 `@import` 和样式规则
- HTML 的 `<!DOCTYPE html>`

正确顺序：

```
文件头注释
  ↓
#pragma once / import / package
  ↓
代码正文
```

---

## 二、文件头注释格式

### 必须包含的字段

| 字段 | 说明 | 示例 |
|------|------|------|
| `@file` | 文件名 | `FactorEngine.hpp` |
| `@brief` | 一句话模块说明 | `因子计算引擎` |
| `层级` | 项目结构中的目录层级 | `backend/calcFactors/runtime/` |
| `项目内绝对路径` | 从项目根开始的完整路径，不含盘符 | `backend/calcFactors/runtime/FactorEngine.hpp` |
| `模块作用` | 这个模块具体做什么 | `滚动计算双均线、唐奇安通道、ATR` |
| `使用者` | 谁调用或依赖本模块 | `StrategyEngine 依赖本模块` |
| `项目角色` | 在整体架构中扮演什么角色 | `C++ 信号链路的计算起点` |
| `引入说明` | 依赖了什么头文件、包或库 | `依赖 config/Kline.hpp` |
| `维护记录` | 日期 + 模块级变更 | `2026-08-27 初始创建` |

### 字段顺序

固定为：

```
@file
@brief
层级
项目内绝对路径
模块作用
使用者
项目角色
引入说明
维护记录
```

---

## 三、各语言完整示例

### C++ 头文件

```cpp
/**
 * @file FactorEngine.hpp
 * @brief 因子计算引擎
 *
 * 层级：
 *   backend/calcFactors/runtime/
 *
 * 项目内绝对路径：
 *   backend/calcFactors/runtime/FactorEngine.hpp
 *
 * 模块作用：
 *   接收逐根K线，滚动计算双均线、唐奇安通道、ATR 等技术因子。
 *
 * 使用者：
 *   StrategyEngine 依赖本模块输出的因子生成交易信号。
 *
 * 项目角色：
 *   C++ 信号链路的计算起点，所有技术指标在此产出。
 *
 * 引入说明：
 *   依赖 config/Kline.hpp 和 config/StrategyConfig.hpp。
 *   依赖标准库 deque、algorithm、numeric、cmath。
 *
 * 维护记录：
 *   2026-08-27 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 规整注释
 */

#pragma once

#include <deque>
#include <algorithm>

namespace runtime {
...
}
```

### Python 模块

```python
"""
@file dataset.py
@brief 训练数据集构建

层级：
    backend/YukiPilot/train/

项目内绝对路径：
    backend/YukiPilot/train/dataset.py

模块作用：
    从 runtime_files/klines/ 读取 JSONL K线数据，
    构建因子特征矩阵，滑窗切分并生成三分类标签。

使用者：
    YukiPilot/train/trainer.py 和 YukiPilot/main.py 依赖本模块。

项目角色：
    YukiPilot 训练流水线的数据入口，
    负责把原始K线转换为模型可用的训练样本。

引入说明：
    依赖 numpy，
    依赖 ..config.agent_config，
    依赖 ..dataclass.schemas。

维护记录：
    2026-08-28 初始创建
    2026-08-29 项目由 calcAgent 改名为 YukiPilot
"""

import json
import numpy as np
```

### Go 文件

```go
/*
@file snapshot.go
@brief 快照管理器

层级：
    backend/netService/goServer/

项目内绝对路径：
    backend/netService/goServer/snapshot.go

模块作用：
    从 runtime_files/ 读取持仓、信号、组合快照，
    合并成 PortfolioSnapshot，并提供线程安全的缓存读取。

使用者：
    main.go 各 HTTP handler 通过 SnapshotManager.Get() 获取最新快照。

项目角色：
    Go 服务的数据聚合层，负责把运行时文件翻译成前端可用的 JSON。

引入说明：
    依赖标准库 encoding/json、fmt、log、os、path/filepath、sort、sync、time。

维护记录：
    2026-08-28 初始创建
    2026-08-29 增加训练进度与思考状态读取
*/

package main

import (
    "encoding/json"
    "fmt"
)
```

### JavaScript 文件

```javascript
/**
 * @file bus.js
 * @brief 极简事件总线，挂在 window.App.bus
 *
 * 层级：
 *   frontend/src/api/
 *
 * 项目内绝对路径：
 *   frontend/src/api/bus.js
 *
 * 模块作用：
 *   提供 on / emit 订阅发布能力，所有前端模块通过事件解耦通信。
 *
 * 使用者：
 *   app.js 和各 dom 页面模块通过 App.bus 通信。
 *
 * 项目角色：
 *   前端通信基座，是页面与数据层之间的消息中枢。
 *
 * 引入说明：
 *   无依赖，IIFE 挂载到 window.App。
 *
 * 维护记录：
 *   2026-08-28 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 统一文件头
 */

(function () {
  'use strict';
  ...
})();
```

### CSS 文件

```css
/**
 * @file tokens.css
 * @brief 二次元设计令牌
 *
 * 层级：
 *   frontend/src/style/
 *
 * 项目内绝对路径：
 *   frontend/src/style/tokens.css
 *
 * 模块作用：
 *   定义全局 CSS 变量，作为所有样式层的唯一设计来源。
 *
 * 使用者：
 *   base.css 和 components.css 引用本文件中的变量。
 *
 * 项目角色：
 *   前端设计令牌层，加载顺序排第一。
 *
 * 引入说明：
 *   无外部依赖，仅定义 :root 变量。
 *
 * 维护记录：
 *   2026-08-28 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 统一文件头
 */

:root {
  --cream: #fdf6f0;
}
```

### HTML 文件

```html
<!--
@file index.html
@brief 前端单页入口与页面模板

层级：
    frontend/public/

项目内绝对路径：
    frontend/public/index.html

模块作用：
    提供 SPA 外壳、四页 hash 路由模板、行模板与装饰层。

使用者：
    浏览器直接打开或由 Go 静态托管访问。

项目角色：
    前端监控面板唯一入口，所有页面模板与脚本加载在此汇聚。

引入说明：
    依赖 src/style/ 下 tokens、base、components 三份样式。
    依赖 src/api/ 与 src/dom/ 下脚本按固定顺序加载。

维护记录：
    2026-08-28 初始创建
    2026-08-29 二次元风格改版
-->

<!DOCTYPE html>
<html lang="zh-CN">
...
</html>
```

### HTML 模板片段

```html
<!--
@file signal-row.html
@brief 信号表行模板

层级：
    frontend/src/templates/

项目内绝对路径：
    frontend/src/templates/signal-row.html

模块作用：
    提供信号表行模板，供 page_monitor.js 克隆渲染。

使用者：
    page_monitor.js 渲染实时信号表时使用。

项目角色：
    前端信号行模板，与 index.html 内联副本逐字节一致。

引入说明：
    无脚本依赖，仅包含 HTML template 结构。

维护记录：
    2026-08-28 初始创建
    2026-08-29 按 docs/COMMENT_STYLE.md 统一文件头
-->

<template id="tpl-signal-row">
  ...
</template>
```

---

## 四、类 / 结构体 / 函数注释

### 1. 类注释

用 Doxygen 格式 `/** @brief ... */`，写清楚它是什么、给谁用。

```cpp
/**
 * @brief 缓存行对齐的 K 线结构体
 *
 * 字段布局（64 位系统标准 ABI）：
 * ...
 * 排序语义：先按股票代码，再按日期。
 */
struct alignas(64) Kline {
    ...
};
```

### 2. 公共函数注释

必须写清楚 `@brief`、`@param`、`@return`，必要时加 `@note`。

```cpp
/**
 * @brief 加载单只股票的历史K线
 * @param filepath 文件完整路径
 * @return 成功返回 K 线序列；失败返回 std::nullopt
 * @note 同步读取，不走线程池，适合调试与单元测试
 */
std::optional<std::vector<config::Kline>> LoadKlines(const char* filepath);
```

Python 公共函数用 docstring：

```python
def fuse_decision(cpp_signal, model_probs, model_conf, agent_cfg):
    """融合模型输出与 C++ 信号，返回 (decision, confidence, reasons)。

    参数:
        cpp_signal: SignalOutput 对象 / dict / 字符串 / None。
        model_probs: 各类别概率，形如 [3] 或 [1, 3]。
        model_conf: 模型置信度标量。
        agent_cfg: AgentConfig。

    返回:
        (final_decision, confidence, reason_parts)
    """
```

### 3. 私有函数注释

除非逻辑复杂，否则只写一行 `//` 注释。不用 Doxygen 格式。

```cpp
// 计算TR，首根K线用 high-low，其余用三值最大值
double calcTrueRange(const Kline& k) const;

// 从扁平 JSON 里取数值字段，容忍数字带引号
std::optional<double> get_number(const std::string& json, const char* key);
```

### 4. 字段注释

结构体成员用行内 `//` 注释：

```cpp
struct Position {
    int64_t quantity = 0;             // 持仓数量
    double avgCost = 0.0;             // 平均成本
    double currentPrice = 0.0;        // 最新价
    double floatingPnl = 0.0;         // 浮动盈亏
};
```

不用 `///<`，统一用 `//`。

---

## 五、跨语言契约注释

涉及 bridge.hpp 或运行时文件的字段，必须注明"契约"。

```cpp
// bridge.hpp 契约字段：因子文件统一使用 ma_short / ma_long / atr
inline constexpr const char* FIELD_MA_SHORT = "ma_short";
```

如果修改字段名，必须同步更新：

- C++ bridge.hpp
- Python dataclass/schemas.py
- Go 结构体 tag
- 前端 data-f 字段
- 相关文档

改动字段的唯一入口是 `backend/netService/bridge.hpp`。

---

## 六、维护日期规范

### 格式

统一使用 `YYYY-MM-DD`。

### 位置

放在文件头注释的"维护记录"里，按时间正序排列。

### 内容

只记录模块级变化，不记录小改。

例如：

```
维护记录：
  2026-08-27 初始创建
  2026-08-28 增加移动语义
  2026-08-29 修复移动赋值泄漏
```

如果只是改注释、改格式，不需要写维护记录。

---

## 七、层级与路径规范

### 层级格式

只写到目录，不写到文件名：

```
backend/calcFactors/runtime/
frontend/src/dom/
```

### 项目内绝对路径格式

写到具体文件名，不含盘符：

```
backend/calcFactors/runtime/FactorEngine.hpp
frontend/src/dom/chart.js
```

### 规则

- 路径以项目根目录开始
- 不使用 `./` 或 `../`
- 不包含 `C:`、`/home/user/` 等本地绝对路径前缀
- 目录用 `/` 分隔，不用 `\`

---

## 八、不适用本规范的文件

以下文件类型不加文件头注释：

| 类型 | 说明 |
|------|------|
| SVG | 纯矢量素材，内部注释保留即可 |
| go.mod | Go 模块定义 |
| requirements.txt | Python 依赖清单，可加单行 `#` 说明 |
| .gitignore | Git 配置 |
| 图片与字体 | 二进制资源 |

配置文件如需说明，只加一行简短注释，不套用完整文件头格式。

---

## 九、负面清单

以下写法一律不用：

- `用户视角：`
- `职责边界：`
- `内部工具，外部禁止调用。`
- `设计说明：`
- `注意：` 滥用
- 超过 5 行的函数注释
- 重复代码本身的注释
- `----------` 分隔线（C++ 单文件版除外）
- `///<` 字段注释

---

## 十、维护约定

1. 新建文件必须写文件头注释，包含全部必需字段。
2. 修改模块作用、使用者、项目角色时，必须同步更新文件头。
3. 文件移动或重命名时，必须更新「层级」和「项目内绝对路径」。
4. 跨语言字段修改，必须更新所有相关注释和文档。
5. 维护记录只写模块级变化，不写琐碎修改。
6. 发现 AI 模板腔注释，立即改掉。
7. 每个模块的注释，半年后自己还能看懂，才算合格。
8. 文件头永远在文件最顶部，先于所有代码指令。