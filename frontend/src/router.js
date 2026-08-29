/**
 * @file router.js
 * @brief hash 路由，挂在 window.App.router
 *
 * 层级：
 *   frontend/src/
 *
 * 项目内绝对路径：
 *   frontend/src/router.js
 *
 * 模块作用：
 *   管理四页 hash 路由切换，包括页面模板克隆、出入场动画、
 *   导航激活态同步和页面模块的 mount/unmount 生命周期。
 *
 * 使用者：
 *   app.js 在启动时调用 App.router.init()。
 *
 * 项目角色：
 *   前端页面切换中枢，连接导航、模板和页面模块。
 *
 * 引入说明：
 *   依赖 App.pages 下各页面模块提供的 mount/unmount 接口。
 *
 * 维护记录：
 *   2026-08-28 初始创建
 *   2026-08-29 按 docs/COMMENT_STYLE.md 统一文件头
 */

(function () {
  'use strict';

  window.App = window.App || {};
  var App = window.App;
  App.pages = App.pages || {};

  var DEFAULT_PAGE = 'dashboard';
  var OUT_MS = 150;
  var VALID = ['dashboard', 'monitor', 'training', 'agent'];

  var current = null;
  var switching = false;
  var pending = null;

  function reducedMotion() {
    return !!(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches);
  }

  function parseHash() {
    var raw = (window.location.hash || '').replace(/^#\/?/, '');
    return raw || DEFAULT_PAGE;
  }

  function updateNav(page) {
    var items = document.querySelectorAll('#sidenav .nav-item');
    for (var i = 0; i < items.length; i++) {
      items[i].classList.toggle('active', items[i].getAttribute('data-page') === page);
    }
  }

  function mountPage(container, page) {
    var tpl = document.querySelector('template[data-page="' + page + '"]');
    if (!tpl) {
      console.error('[router] 找不到页面模板:', page);
      return;
    }
    var frag = tpl.content.cloneNode(true);
    var node = frag.firstElementChild;
    if (node && !reducedMotion()) node.classList.add('page-enter');
    container.appendChild(frag);
    current = page;
    updateNav(page);
    var mod = App.pages[page];
    if (mod && typeof mod.mount === 'function') {
      mod.mount(container.firstElementChild);
    }
  }

  function render(page) {
    var container = document.getElementById('page');
    if (!container) return;

    var oldMod = current && App.pages[current];
    if (oldMod && typeof oldMod.unmount === 'function') {
      try { oldMod.unmount(); } catch (err) { console.error('[router] unmount 出错:', err); }
    }
    current = null;

    function finish() {
      container.innerHTML = '';
      mountPage(container, page);
      switching = false;
      if (pending && pending !== page) {
        var next = pending;
        pending = null;
        switching = true;
        render(next);
      } else {
        pending = null;
      }
    }

    var oldNode = container.firstElementChild;
    if (oldNode && !reducedMotion()) {
      oldNode.classList.add('page-leave');
      setTimeout(finish, OUT_MS);
    } else {
      finish();
    }
  }

  function onHashChange() {
    var page = parseHash();
    if (VALID.indexOf(page) === -1) {
      window.location.replace('#/' + DEFAULT_PAGE);
      return;
    }
    if (switching) {
      if (page !== pending) pending = page;
      return;
    }
    if (page === current) return;
    switching = true;
    render(page);
  }

  function init() {
    if (!window.location.hash) {
      window.location.replace('#/' + DEFAULT_PAGE);
    }
    window.addEventListener('hashchange', onHashChange);
    onHashChange();
  }

  App.router = { init: init };
})();