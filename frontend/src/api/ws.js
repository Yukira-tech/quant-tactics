/**
 * @file ws.js
 * @brief 实时数据通道，挂在 window.App.WsClient
 *
 * 层级：
 *   frontend/src/api/
 *
 * 项目内绝对路径：
 *   frontend/src/api/ws.js
 *
 * 模块作用：
 *   管理 WebSocket 连接、指数退避重连、HTTP 轮询降级与状态广播。
 *
 * 使用者：
 *   app.js 引导时创建 App.WsClient 并调用 start()。
 *
 * 项目角色：
 *   前端实时通信核心，保证面板在 WS 异常时仍能通过 HTTP 轮询拿到数据。
 *
 * 引入说明：
 *   依赖 endpoints.js 中的 App.API.ws 地址。
 *   依赖 http.js 的 App.http.snapshot 做首帧与轮询。
 *
 * 维护记录：
 *   2026-08-28 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 统一文件头
 */

(function () {
  'use strict';

  window.App = window.App || {};

  var BACKOFF_DELAYS = [1000, 2000, 4000, 8000, 16000];
  var MAX_WS_FAILS = BACKOFF_DELAYS.length;
  var POLL_INTERVAL = 3000;
  var WS_RESUME_INTERVAL = 30000;
  var MAX_HTTP_FAILS = 3;

  var STATUS = { LIVE: 'live', POLLING: 'polling', OFFLINE: 'offline' };

  function WsClient(opts) {
    opts = opts || {};
    this._backoffDelays = opts.backoffDelays || BACKOFF_DELAYS;
    this._maxWsFails = this._backoffDelays.length;
    this._pollInterval = opts.pollInterval || POLL_INTERVAL;
    this._resumeInterval = opts.resumeInterval || WS_RESUME_INTERVAL;
    this._maxHttpFails = opts.maxHttpFails || MAX_HTTP_FAILS;

    this._stopped = true;
    this._status = null;
    this._polling = false;

    this._ws = null;
    this._wsFails = 0;
    this._httpFails = 0;

    this._reconnectTimer = null;
    this._pollTimer = null;
    this._resumeTimer = null;
  }

  WsClient.prototype.start = function () {
    if (!this._stopped) return;
    this._stopped = false;
    this._wsFails = 0;
    this._httpFails = 0;

    var self = this;
    window.App.http.snapshot()
      .then(function (data) { self._onHttpFrame(data); })
      .catch(function (err) { self._onHttpError(err); });

    this._connectWs();
  };

  WsClient.prototype.stop = function () {
    this._stopped = true;
    this._clearTimer('_reconnectTimer');
    this._clearTimer('_pollTimer');
    this._clearTimer('_resumeTimer');
    if (this._ws) {
      this._ws.onopen = this._ws.onmessage = this._ws.onerror = this._ws.onclose = null;
      try { this._ws.close(); } catch (e) { /* 忽略 */ }
      this._ws = null;
    }
  };

  WsClient.prototype._setStatus = function (status) {
    if (this._stopped || this._status === status) return;
    this._status = status;
    window.App.bus.emit('status', status);
  };

  WsClient.prototype._connectWs = function (isResume) {
    if (this._stopped) return;
    this._clearTimer('_reconnectTimer');

    var self = this;
    var ws;
    try {
      ws = new WebSocket(window.App.API.ws);
    } catch (e) {
      this._onWsClosed(isResume);
      return;
    }
    this._ws = ws;

    ws.onopen = function () {
      if (self._stopped) return;
      self._wsFails = 0;
      self._httpFails = 0;
      self._stopPolling();
      self._setStatus(STATUS.LIVE);
    };

    ws.onmessage = function (ev) {
      if (self._stopped) return;
      var data;
      try {
        data = JSON.parse(ev.data);
      } catch (e) {
        console.warn('[ws] 帧不是合法 JSON，已忽略:', e);
        return;
      }
      window.App.bus.emit('snapshot', data);
    };

    ws.onerror = function () {
      try { ws.close(); } catch (e) { /* 忽略 */ }
    };

    ws.onclose = function () {
      if (self._stopped) return;
      if (self._ws === ws) self._ws = null;
      self._onWsClosed(isResume);
    };
  };

  WsClient.prototype._onWsClosed = function (isResume) {
    if (this._stopped) return;

    if (isResume && this._polling) {
      return;
    }

    this._wsFails += 1;
    if (this._wsFails > this._maxWsFails) {
      this._enterPolling();
      return;
    }

    var delay = this._backoffDelays[this._wsFails - 1];
    var self = this;
    this._reconnectTimer = setTimeout(function () {
      self._connectWs(false);
    }, delay);
  };

  WsClient.prototype._enterPolling = function () {
    if (this._stopped || this._polling) return;
    this._polling = true;
    this._setStatus(STATUS.POLLING);
    this._pollOnce();

    var self = this;
    this._resumeTimer = setInterval(function () {
      self._connectWs(true);
    }, this._resumeInterval);
  };

  WsClient.prototype._stopPolling = function () {
    this._polling = false;
    this._clearTimer('_pollTimer');
    this._clearTimer('_resumeTimer');
  };

  WsClient.prototype._pollOnce = function () {
    if (this._stopped || !this._polling) return;
    var self = this;
    window.App.http.snapshot()
      .then(function (data) { self._onHttpFrame(data); })
      .catch(function (err) { self._onHttpError(err); })
      .then(function () {
        if (!self._stopped && self._polling) {
          self._pollTimer = setTimeout(function () { self._pollOnce(); }, self._pollInterval);
        }
      });
  };

  WsClient.prototype._onHttpFrame = function (data) {
    if (this._stopped) return;
    this._httpFails = 0;
    window.App.bus.emit('snapshot', data);
    if (this._status === STATUS.OFFLINE) {
      this._setStatus(this._ws && this._ws.readyState === 1 ? STATUS.LIVE : STATUS.POLLING);
    }
  };

  WsClient.prototype._onHttpError = function (err) {
    if (this._stopped) return;
    this._httpFails += 1;
    console.warn('[http] 快照请求失败（' + this._httpFails + '/' + this._maxHttpFails + '）:',
      err && err.message ? err.message : err);
    if (this._httpFails >= this._maxHttpFails) {
      this._setStatus(STATUS.OFFLINE);
    }
  };

  WsClient.prototype._clearTimer = function (key) {
    if (this[key]) {
      clearTimeout(this[key]);
      clearInterval(this[key]);
      this[key] = null;
    }
  };

  WsClient.STATUS = STATUS;
  WsClient.MAX_WS_FAILS = MAX_WS_FAILS;
  WsClient.MAX_HTTP_FAILS = MAX_HTTP_FAILS;

  window.App.WsClient = WsClient;
})();