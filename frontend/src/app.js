/**
 * @file app.js
 * @brief SPA 引导入口，最后加载
 *
 * 层级：
 *   frontend/src/
 *
 * 项目内绝对路径：
 *   frontend/src/app.js
 *
 * 模块作用：
 *   在 DOM 就绪后启动加载层、数据通道和路由。
 *   负责连接状态徽章三态切换和加载层的淡出移除。
 *
 * 使用者：
 *   frontend/public/index.html 最后引入本文件。
 *
 * 项目角色：
 *   前端启动协调器，串联 loader、WsClient 和 router。
 *
 * 引入说明：
 *   依赖全局事件总线 App.bus、WsClient 和 App.router。
 *
 * 维护记录：
 *   2026-08-28 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 统一文件头
 */

(function () {
  'use strict';

  window.App = window.App || {};
  var App = window.App;

  var LOADER_MIN_MS = 600;

  var STATUS_TEXT = {
    live: 'LIVE',
    polling: 'POLLING',
    offline: 'OFFLINE'
  };

  function reducedMotion() {
    return !!(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches);
  }

  function onStatus(status) {
    var badge = document.getElementById('conn-badge');
    if (badge) {
      badge.setAttribute('data-state', status);
      badge.textContent = STATUS_TEXT[status] || 'OFFLINE';
    }
    var page = document.getElementById('page');
    if (page) page.classList.toggle('is-offline', status === 'offline');
  }

  function hideLoader() {
    var loader = document.getElementById('loader');
    if (!loader) return;
    if (reducedMotion()) {
      loader.remove();
      return;
    }
    loader.classList.add('loader-hide');
    loader.addEventListener('animationend', function h() {
      loader.removeEventListener('animationend', h);
      loader.remove();
    });
    loader.addEventListener('transitionend', function h2() {
      loader.removeEventListener('transitionend', h2);
      if (loader.parentNode) loader.remove();
    });
    setTimeout(function () { if (loader.parentNode) loader.remove(); }, 1500);
  }

  function boot() {
    var t0 = Date.now();

    App.bus.on('status', onStatus);

    var client = new App.WsClient();
    client.start();
    App.router.init();

    App.client = client;

    var remain = Math.max(0, LOADER_MIN_MS - (Date.now() - t0));
    setTimeout(hideLoader, remain);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', boot);
  } else {
    boot();
  }
})();