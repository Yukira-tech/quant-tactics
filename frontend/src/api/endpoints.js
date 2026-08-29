/**
 * @file endpoints.js
 * @brief 后端地址配置，挂在 window.App.API
 *
 * 层级：
 *   frontend/src/api/
 *
 * 项目内绝对路径：
 *   frontend/src/api/endpoints.js
 *
 * 模块作用：
 *   集中管理 HTTP 与 WebSocket 服务地址，供 http.js 和 ws.js 使用。
 *
 * 使用者：
 *   http.js 拼接请求前缀，ws.js 建立 WebSocket 连接。
 *
 * 项目角色：
 *   前端通信配置入口，后端地址变化时只改这里。
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

  window.App = window.App || {};

  window.App.API = {
    http: 'http://localhost:8080',
    ws: 'ws://localhost:8080/ws'
  };
})();