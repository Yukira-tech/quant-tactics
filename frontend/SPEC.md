# 前端层内部接口说明

维护者：Yukira  
适用：frontend/  
跨语言字段契约：以 `docs/CONTRACT.md` 与后端 JSON 字段为准

---

## 1. 模块定位

前端是一个无框架的原生 SPA 监控面板，负责把 Go 服务器提供的快照数据实时展示给用户。

它只做三件事：

- 通过 HTTP / WebSocket 获取数据
- 通过 hash 路由在四个页面间切换
- 把数据渲染成统计卡、曲线、表格和时间线

不参与任何计算与决策，不直接读写 `runtime_files/`。

---

## 2. 文件结构

| 文件 | 职责 |
|------|------|
| `public/index.html` | SPA 外壳、页面模板、行模板、装饰层 |
| `src/api/bus.js` | 事件总线，挂载 `App.bus` |
| `src/api/endpoints.js` | 后端地址配置，挂载 `App.API` |
| `src/api/http.js` | HTTP 请求封装，挂载 `App.http` |
| `src/api/ws.js` | WebSocket 客户端，挂载 `App.WsClient` |
| `src/router.js` | hash 路由，挂载 `App.router` |
| `src/dom/page_dashboard.js` | 仪表盘页模块 |
| `src/dom/page_monitor.js` | 行情监控页模块 |
| `src/dom/page_training.js` | 训练中心页模块 |
| `src/dom/page_agent.js` | Agent 思考页模块 |
| `src/app.js` | 启动引导，最后加载 |
| `src/style/tokens.css` | 设计令牌 |
| `src/style/base.css` | 基础样式 |
| `src/style/components.css` | 组件样式 |
| `src/templates/*.html` | 行模板权威副本 |
| `src/image/*.svg` | 装饰与图标素材 |

脚本加载顺序固定，全部挂载到 `window.App` 命名空间。

---

## 3. 全局命名空间契约

所有前端模块都挂在 `window.App` 下：

```javascript
window.App = {
  bus: ...,        // 事件总线
  API: ...,        // 后端地址
  http: ...,       // HTTP 方法
  WsClient: ...,   // WebSocket 客户端类
  router: ...,     // hash 路由
  pages: {},       // 各页面模块
  client: null     // 调试钩子，指向 WsClient 实例
}
```

---

## 4. 事件总线

文件：`src/api/bus.js`

```javascript
App.bus.on(event, fn)     // 订阅事件，返回解绑函数
App.bus.emit(event, data) // 发布事件
```

### 核心事件

| 事件 | 数据 | 说明 |
|------|------|------|
| `snapshot` | PortfolioSnapshot | 完整快照，WS 帧与 HTTP 帧同构 |
| `status` | `live` / `polling` / `offline` | 连接状态变化 |
| `booted` | 无 | 开机引导完成（当前版本保留，未强制使用） |

`App.bus.on('*', fn)` 可监听所有事件，`fn(event, data)`。

---

## 5. HTTP 层

文件：`src/api/http.js`

```javascript
App.http.ApiError            // 自定义错误类，带 status 字段
App.http.fetchJSON(path)     // Promise，5 秒超时，非 2xx 抛错
App.http.snapshot()          // GET /api/snapshot
App.http.portfolio()         // GET /api/portfolio
App.http.signals()           // GET /api/signals
```

### 后端接口

| 方法 | 路径 | 返回 |
|------|------|------|
| GET | `/api/snapshot` | 完整 PortfolioSnapshot |
| GET | `/api/portfolio` | 组合子集 |
| GET | `/api/signals` | 信号数组 |
| GET | `/api/training` | 训练进度，无训练时 `{"state":"idle"}` |
| GET | `/api/thinking` | 全部思考状态，空时 `[]` |

请求超时 5 秒。`ApiError.status` 为 0 表示网络层失败或超时。

---

## 6. WebSocket 层

文件：`src/api/ws.js`

```javascript
var client = new App.WsClient();
client.start();   // 首帧 HTTP + 发起 WS
client.stop();    // 清理定时器与连接
```

### 连接行为

| 状态 | 行为 |
|------|------|
| WS 断开 | 1s/2s/4s/8s/16s 指数退避重连 |
| 连续 5 次失败 | 降级 HTTP 轮询，每 3 秒一次 |
| 轮询期间 | 每 30 秒尝试恢复 WS |
| HTTP 连续 3 次失败 | 状态置 `offline`，页面置灰不清空 |
| HTTP 恢复 | 回到当前通道对应状态 |

状态变化通过 `App.bus.emit('status', status)` 广播。

---

## 7. 路由层

文件：`src/router.js`

```javascript
App.router.init()   // 监听 hashchange，渲染首屏
```

### 合法路由

| hash | 页面 |
|------|------|
| `#/dashboard` | 仪表盘 |
| `#/monitor` | 行情监控 |
| `#/training` | 训练中心 |
| `#/agent` | Agent 思考 |

空 hash 自动补 `#/dashboard`，未知 hash 重定向到 `#/dashboard`。

### 切换流程

```
hashchange
  → 旧页面 unmount()
  → 旧内容淡出 150ms
  → 从 template[data-page] 克隆新内容
  → 新页面 mount(root)
```

页面模块契约：

```javascript
App.pages[page] = {
  mount: function (rootEl) {},   // 订阅事件，渲染缓存数据
  unmount: function () {}        // 解绑事件，清理定时器
};
```

---

## 8. 页面模块契约

### 8.1 仪表盘

文件：`src/dom/page_dashboard.js`

读取字段：

| data-f | 字段 | 说明 |
|--------|------|------|
| `equity` | `total_equity` | 总权益 |
| `pnl` | `total_pnl` | 总盈亏 |
| `drawdown` | `max_drawdown` | 最大回撤 |
| `positions-count` | `positions.length` | 持仓数 |
| `equity-chart` | `total_equity` | 权益曲线 canvas |
| `signal-brief` | `signals` | 最新 5 条信号 |
| `brief-empty` | — | 信号为空时显示 |

权益曲线使用模块级环形缓冲，容量 120 点，页面切换不丢历史。

### 8.2 行情监控

文件：`src/dom/page_monitor.js`

读取字段：

| data-f | 字段 |
|--------|------|
| `signals-body` | `signals[]` |
| `signals-empty` | — |
| `positions-body` | `positions[]` |
| `positions-empty` | — |
| `target-list` | `target_portfolio[]` |
| `target-empty` | — |

使用行模板：

- `tpl-signal-row`
- `tpl-position-row`
- `tpl-target-item`

### 8.3 训练中心

文件：`src/dom/page_training.js`

读取字段：

| data-f | 字段 |
|--------|------|
| `train-empty` | 无训练数据时显示 |
| `train-body` | 有训练数据时显示 |
| `train-state` | `training.state` |
| `epoch-bar` | `epoch / total_epochs` |
| `batch-bar` | `batch / total_batches` |
| `loss` | `loss` |
| `val-loss` | `val_loss` |
| `speed` | `speed` |
| `eta` | `eta_seconds` |
| `loss-chart` | `history[]` |
| `epoch-body` | `history[]` |
| `epoch-empty` | — |

使用行模板：`tpl-epoch-row`。

### 8.4 Agent 思考

文件：`src/dom/page_agent.js`

读取字段：

| data-f | 字段 |
|--------|------|
| `agent-empty` | 无思考数据时显示 |
| `think-grid` | 思考卡片容器 |

使用行模板：`tpl-think-card`。

事件类型：

| type | 渲染 |
|------|------|
| `status` | 圆点行 |
| `tool` | 工具芯片，`data-state="running"` |
| `tool_result` | 工具芯片完成，`data-state="done"` |
| `text` | 打字机文本，40ms/字 |

最终决策：

| 字段 | 说明 |
|------|------|
| `final_decision` | buy/sell/hold |
| `confidence` | 0~1，显示为百分比 |
| `reason` | 中文理由 |

---

## 9. 模板契约

所有行模板在 `src/templates/` 与 `index.html` 内联副本**逐字节一致**。

| 模板 | data-f 字段 |
|------|-------------|
| `tpl-signal-row` | symbol / signal / strength / strength-text / source / time |
| `tpl-position-row` | symbol / quantity / avg_cost / current_price / floating_pnl / inventory_available / inventory_frozen |
| `tpl-target-item` | symbol / qty / bar |
| `tpl-epoch-row` | epoch / train_loss / train_acc / val_loss / val_acc |
| `tpl-think-card` | symbol / state / timeline / final / decision / confidence / reason |

---

## 10. 样式层

### 加载顺序

```
tokens.css → base.css → components.css
```

### tokens.css

所有颜色、字体、圆角、边框、阴影、缓动都来自 CSS 变量，禁止组件层直接写死数值。

### base.css

只负责 reset、字体、滚动条和 reduced-motion，不承载组件样式。

### components.css

全部组件类，包罗导航、卡片、徽章、表格、进度条、思考卡、装饰层、加载层。

---

## 11. 数据流

```
Go 服务器
  ├── HTTP /api/snapshot
  └── WebSocket /ws
        ↓
WsClient
  ├── 正常：WS 推送
  └── 异常：HTTP 轮询
        ↓
App.bus.emit('snapshot', data)
        ↓
各页面模块 onSnapshot()
        ↓
渲染 DOM / Canvas
```

---

## 12. 错误处理约定

- WS 断开自动重连，不中断 UI
- HTTP 超时或失败保留旧数据，不清空页面
- 渲染时字段缺失用 `--` 或空态兜底
- 单页面模块错误不影响其他模块
- reduced-motion 环境下跳过入场、退场、脉冲等非必要动画

任何数据异常都不允许让页面白屏。