/**
 * @file http.js
 * @brief HTTP 请求封装，挂在 window.App.http
 *
 * 层级：
 *   frontend/src/api/
 *
 * 项目内绝对路径：
 *   frontend/src/api/http.js
 *
 * 模块作用：
 *   提供带超时控制的 fetchJSON 以及 snapshot / portfolio / signals
 *   三个快捷请求方法。
 *
 * 使用者：
 *   ws.js 降级轮询时使用 http.snapshot()。
 *   页面模块可通过 App.http 获取 HTTP 数据。
 *
 * 项目角色：
 *   前端 HTTP 通信层，所有 REST 请求统一走这里。
 *
 * 引入说明：
 *   依赖 endpoints.js 中的 App.API.http 前缀。
 *
 * 维护记录：
 *   2026-08-28 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 统一文件头
 */

(function () {
  'use strict';

  window.App = window.App || {};

  var TIMEOUT_MS = 5000;

  /**
   * 自定义 API 错误类，附带 HTTP 状态码。
   * status 为 0 表示请求未得到响应，例如超时或网络层失败。
   */
  function ApiError(message, status) {
    this.name = 'ApiError';
    this.message = message;
    this.status = status || 0;
    if (Error.captureStackTrace) {
      Error.captureStackTrace(this, ApiError);
    } else {
      this.stack = new Error(message).stack;
    }
  }
  ApiError.prototype = Object.create(Error.prototype);
  ApiError.prototype.constructor = ApiError;

  /**
   * 请求 JSON 接口。
   * @param {string} path 以 '/' 开头的路径，如 '/api/snapshot'
   * @returns {Promise<Object|Array>} 解析后的 JSON 数据
   * @throws {ApiError} 超时、网络失败或 HTTP 非 2xx
   */
  function fetchJSON(path) {
    var url = window.App.API.http + path;
    var controller = new AbortController();
    var timer = setTimeout(function () {
      controller.abort();
    }, TIMEOUT_MS);

    return fetch(url, { signal: controller.signal })
      .then(function (resp) {
        clearTimeout(timer);
        if (!resp.ok) {
          throw new ApiError(
            'HTTP ' + resp.status + ' ' + resp.statusText + ' @ ' + path,
            resp.status
          );
        }
        return resp.json();
      })
      .catch(function (err) {
        clearTimeout(timer);
        if (err instanceof ApiError) {
          throw err;
        }
        if (err && err.name === 'AbortError') {
          throw new ApiError('请求超时（' + TIMEOUT_MS + 'ms）: ' + path, 0);
        }
        throw new ApiError('网络错误: ' + (err && err.message ? err.message : String(err)) + ' @ ' + path, 0);
      });
  }

  window.App.http = {
    ApiError: ApiError,
    fetchJSON: fetchJSON,
    // 完整快照，与 WebSocket 推送帧同构
    snapshot: function () { return fetchJSON('/api/snapshot'); },
    // 组合子集
    portfolio: function () { return fetchJSON('/api/portfolio'); },
    // 信号数组
    signals: function () { return fetchJSON('/api/signals'); }
  };
})();