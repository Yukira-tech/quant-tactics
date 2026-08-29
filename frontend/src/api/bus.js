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

  window.App = window.App || {};

  // 事件名 -> Set<回调函数>
  var listeners = new Map();

  function on(event, fn) {
    if (typeof fn !== 'function') {
      throw new TypeError('App.bus.on: 回调必须是函数');
    }
    if (!listeners.has(event)) {
      listeners.set(event, new Set());
    }
    listeners.get(event).add(fn);
    // 返回解绑函数
    return function off() {
      var set = listeners.get(event);
      if (set) {
        set.delete(fn);
        if (set.size === 0) listeners.delete(event);
      }
    };
  }

  function emit(event, data) {
    // 先精确事件，再通配 '*'
    var exact = listeners.get(event);
    if (exact) {
      exact.forEach(function (fn) {
        try {
          fn(data);
        } catch (err) {
          console.error('[bus] 事件 "' + event + '" 的回调执行出错:', err);
        }
      });
    }
    var wild = listeners.get('*');
    if (wild) {
      wild.forEach(function (fn) {
        try {
          fn(event, data);
        } catch (err) {
          console.error('[bus] 通配回调执行出错:', err);
        }
      });
    }
  }

  window.App.bus = { on: on, emit: emit };
})();
