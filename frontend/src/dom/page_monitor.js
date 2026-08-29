/**
 * @file page_monitor.js
 * @brief 行情监控页，挂在 window.App.pages.monitor
 *
 * 层级：
 *   frontend/src/dom/
 *
 * 项目内绝对路径：
 *   frontend/src/dom/page_monitor.js
 *
 * 模块作用：
 *   渲染完整信号表、持仓表和目标组合，空态自动切换显示。
 *
 * 使用者：
 *   router.js 在 #/monitor 页面挂载时调用 mount()。
 *
 * 项目角色：
 *   前端行情监控页专属渲染模块，订阅 snapshot 事件驱动 UI 更新。
 *
 * 引入说明：
 *   依赖全局事件总线 App.bus。
 *   使用 index.html 中的 tpl-signal-row、tpl-position-row、tpl-target-item 模板。
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

  var STAGGER_MS = 70;

  var root = null;
  var offSnapshot = null;

  var fmtInt = new Intl.NumberFormat('zh-CN', { maximumFractionDigits: 0 });
  var fmtMoney = new Intl.NumberFormat('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 });

  function field(name) { return root && root.querySelector('[data-f="' + name + '"]'); }

  function cloneRow(tplId) {
    var tpl = document.getElementById(tplId);
    if (!tpl) return null;
    var row = tpl.content.firstElementChild.cloneNode(true);
    return {
      row: row,
      f: function (name) { return row.querySelector('[data-f="' + name + '"]'); }
    };
  }

  function sigKind(s) {
    if (s === 'buy' || s === 'cover') return { cls: 'badge-buy', text: '买入' };
    if (s === 'sell' || s === 'short') return { cls: 'badge-sell', text: '卖出' };
    return { cls: 'badge-hold', text: '持有' };
  }

  // 信号表
  function renderSignals(signals) {
    var body = field('signals-body');
    var empty = field('signals-empty');
    if (!body || !empty) return;

    body.innerHTML = '';
    empty.hidden = signals.length > 0;
    body.parentNode.hidden = signals.length === 0;

    signals.forEach(function (sig, i) {
      var c = cloneRow('tpl-signal-row');
      if (!c) return;
      c.row.style.animationDelay = (i * STAGGER_MS) + 'ms';

      var k = sigKind(sig.signal);
      var badge = c.f('signal');
      badge.classList.remove('badge-buy', 'badge-sell', 'badge-hold');
      badge.classList.add(k.cls);
      badge.textContent = k.text;

      c.f('symbol').textContent = sig.symbol || '------';
      var strength = typeof sig.strength === 'number' ? sig.strength : 0;
      c.f('strength').style.width = Math.max(0, Math.min(100, strength * 100)) + '%';
      c.f('strength-text').textContent = strength.toFixed(2);
      c.f('source').textContent = sig.strategy_source || '—';
      c.f('time').textContent = (sig.timestamp || '').split(' ').pop() || '--:--:--';

      body.appendChild(c.row);
    });
  }

  // 持仓表
  function renderPositions(positions) {
    var body = field('positions-body');
    var empty = field('positions-empty');
    if (!body || !empty) return;

    body.innerHTML = '';
    empty.hidden = positions.length > 0;
    body.parentNode.hidden = positions.length === 0;

    positions.forEach(function (pos, i) {
      var c = cloneRow('tpl-position-row');
      if (!c) return;
      c.row.style.animationDelay = (i * STAGGER_MS) + 'ms';

      c.f('symbol').textContent = pos.symbol || '------';
      c.f('quantity').textContent = fmtInt.format(pos.quantity || 0);
      c.f('avg_cost').textContent = fmtMoney.format(pos.avg_cost || 0);
      c.f('current_price').textContent = fmtMoney.format(pos.current_price || 0);

      var pnl = typeof pos.floating_pnl === 'number' ? pos.floating_pnl : 0;
      var pnlEl = c.f('floating_pnl');
      pnlEl.textContent = (pnl >= 0 ? '+' : '') + fmtMoney.format(pnl);
      pnlEl.classList.toggle('up', pnl > 0);
      pnlEl.classList.toggle('down', pnl < 0);

      c.f('inventory_available').textContent = fmtInt.format(pos.inventory_available || 0);
      c.f('inventory_frozen').textContent = fmtInt.format(pos.inventory_frozen || 0);

      body.appendChild(c.row);
    });
  }

  // 目标组合
  function renderTarget(target) {
    var list = field('target-list');
    var empty = field('target-empty');
    if (!list || !empty) return;

    list.innerHTML = '';
    empty.hidden = target.length > 0;
    list.hidden = target.length === 0;

    var max = 0;
    target.forEach(function (t) {
      if (typeof t.target_quantity === 'number' && t.target_quantity > max) max = t.target_quantity;
    });

    target.forEach(function (t, i) {
      var c = cloneRow('tpl-target-item');
      if (!c) return;
      c.row.style.animationDelay = (i * STAGGER_MS) + 'ms';

      var qty = typeof t.target_quantity === 'number' ? t.target_quantity : 0;
      c.f('symbol').textContent = t.symbol || '------';
      c.f('qty').textContent = fmtInt.format(qty);
      c.f('bar').style.width = max > 0 ? (qty / max * 100).toFixed(1) + '%' : '0%';

      list.appendChild(c.row);
    });
  }

  function onSnapshot(snap) {
    if (!root || !snap) return;
    renderSignals(Array.isArray(snap.signals) ? snap.signals : []);
    renderPositions(Array.isArray(snap.positions) ? snap.positions : []);
    renderTarget(Array.isArray(snap.target_portfolio) ? snap.target_portfolio : []);
  }

  App.pages.monitor = {
    mount: function (rootEl) {
      root = rootEl;
      offSnapshot = App.bus.on('snapshot', onSnapshot);
    },
    unmount: function () {
      if (offSnapshot) { offSnapshot(); offSnapshot = null; }
      root = null;
    }
  };
})();