/**
 * @file page_dashboard.js
 * @brief 仪表盘页，挂在 window.App.pages.dashboard
 *
 * 层级：
 *   frontend/src/dom/
 *
 * 项目内绝对路径：
 *   frontend/src/dom/page_dashboard.js
 *
 * 模块作用：
 *   渲染统计卡、权益曲线和最新信号摘要。
 *   权益曲线使用模块级环形缓冲，页面切换不丢历史。
 *
 * 使用者：
 *   router.js 在 #/dashboard 页面挂载时调用 mount()。
 *
 * 项目角色：
 *   前端仪表盘页专属渲染模块，订阅 snapshot 事件驱动 UI 更新。
 *
 * 引入说明：
 *   依赖全局事件总线 App.bus。
 *   使用 index.html 中的统计卡与 canvas 结构。
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

  var CAP = 120;
  var TICK_MS = 1400;
  var BRIEF_LEN = 5;

  var buf = [];
  var root = null;
  var offSnapshot = null;
  var rafId = null;
  var prevStats = {};

  var fmtInt = new Intl.NumberFormat('zh-CN', { maximumFractionDigits: 0 });
  var fmtMoney = new Intl.NumberFormat('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 });

  function cssVar(name, fallback) {
    var v = window.getComputedStyle(document.documentElement).getPropertyValue(name);
    return (v && v.trim()) || fallback;
  }

  function reducedMotion() {
    return !!(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches);
  }

  function field(name) { return root && root.querySelector('[data-f="' + name + '"]'); }

  function sigKind(s) {
    if (s === 'buy' || s === 'cover') return { cls: 'badge-buy', text: '买入' };
    if (s === 'sell' || s === 'short') return { cls: 'badge-sell', text: '卖出' };
    return { cls: 'badge-hold', text: '持有' };
  }

  function setStat(name, value, text) {
    var el = field(name);
    if (!el) return;
    if (typeof value === 'number' && isFinite(value)) {
      var prev = prevStats[name];
      if (prev !== undefined && prev !== value && !reducedMotion()) {
        var cls = value > prev ? 'flash-up' : 'flash-down';
        el.classList.remove('flash-up', 'flash-down');
        void el.offsetWidth;
        el.classList.add(cls);
        el.addEventListener('animationend', function h() {
          el.classList.remove(cls);
          el.removeEventListener('animationend', h);
        });
      }
      prevStats[name] = value;
    }
    el.textContent = text;
  }

  function renderStats(snap) {
    setStat('equity', snap.total_equity, fmtMoney.format(snap.total_equity || 0));

    var pnl = typeof snap.total_pnl === 'number' ? snap.total_pnl : 0;
    var pnlEl = field('pnl');
    if (pnlEl) {
      pnlEl.classList.toggle('up', pnl > 0);
      pnlEl.classList.toggle('down', pnl < 0);
    }
    setStat('pnl', snap.total_pnl, (pnl >= 0 ? '+' : '') + fmtMoney.format(pnl));

    setStat('drawdown', snap.max_drawdown, fmtMoney.format(snap.max_drawdown || 0));

    var n = Array.isArray(snap.positions) ? snap.positions.length : 0;
    setStat('positions-count', n, fmtInt.format(n));
  }

  function setup(canvas) {
    var dpr = window.devicePixelRatio || 1;
    var w = canvas.clientWidth;
    var h = canvas.clientHeight;
    if (!w || !h) return null;
    var pw = Math.round(w * dpr);
    var ph = Math.round(h * dpr);
    if (canvas.width !== pw || canvas.height !== ph) { canvas.width = pw; canvas.height = ph; }
    var ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { ctx: ctx, w: w, h: h };
  }

  function drawEmpty(ctx, w, h) {
    var soft = cssVar('--ink-soft', '#9c8b83');
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = soft;
    ctx.lineWidth = 1.5;
    ctx.setLineDash([6, 6]);
    ctx.strokeRect(1, 1, w - 2, h - 2);
    ctx.setLineDash([]);
    ctx.fillStyle = soft;
    ctx.font = '13px sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('暂无数据 · 等待首帧快照', w / 2, h / 2);
  }

  function draw(canvas, phase) {
    var s = setup(canvas);
    if (!s) return;
    var ctx = s.ctx, w = s.w, h = s.h;
    if (buf.length === 0) { drawEmpty(ctx, w, h); return; }

    var accent = cssVar('--sakura-deep', '#ff9ebb');
    var grid = cssVar('--sakura', '#ffd3e0');

    var padL = 10, padR = 14, padT = 12, padB = 10;
    var iw = w - padL - padR;
    var ih = h - padT - padB;

    var min = Math.min.apply(null, buf);
    var max = Math.max.apply(null, buf);
    if (min === max) { var m = Math.abs(min) * 0.005 || 1; min -= m; max += m; }
    else { var pad = (max - min) * 0.05; min -= pad; max += pad; }

    var n = buf.length;
    var stepX = n > 1 ? iw / (n - 1) : 0;
    function X(i) { return padL + (n > 1 ? i * stepX : iw / 2); }
    function Y(v) { return padT + (max - v) / (max - min) * ih; }

    ctx.clearRect(0, 0, w, h);

    ctx.strokeStyle = grid;
    ctx.lineWidth = 1;
    ctx.setLineDash([5, 5]);
    [1 / 3, 2 / 3].forEach(function (r) {
      var y = padT + ih * r;
      ctx.beginPath();
      ctx.moveTo(padL, y);
      ctx.lineTo(w - padR, y);
      ctx.stroke();
    });
    ctx.setLineDash([]);

    ctx.strokeStyle = accent;
    ctx.lineWidth = 2;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    ctx.beginPath();
    for (var i = 0; i < n; i++) {
      var x = X(i), y = Y(buf[i]);
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.stroke();

    var grad = ctx.createLinearGradient(0, padT, 0, padT + ih);
    grad.addColorStop(0, 'rgba(255,158,187,0.18)');
    grad.addColorStop(1, 'rgba(255,158,187,0)');
    ctx.fillStyle = grad;
    ctx.lineTo(X(n - 1), padT + ih);
    ctx.lineTo(X(0), padT + ih);
    ctx.closePath();
    ctx.fill();

    var lx = X(n - 1), ly = Y(buf[n - 1]);
    var pulse = reducedMotion() ? 0.5 : 0.5 - 0.5 * Math.cos(phase * Math.PI * 2);
    ctx.fillStyle = 'rgba(255,158,187,' + (0.18 + 0.28 * pulse).toFixed(3) + ')';
    ctx.beginPath();
    ctx.arc(lx, ly, 5 + 5 * pulse, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = accent;
    ctx.beginPath();
    ctx.arc(lx, ly, 4, 0, Math.PI * 2);
    ctx.fill();
  }

  function canvasEl() { return root && root.querySelector('[data-f="equity-chart"]'); }

  function tick(now) {
    var canvas = canvasEl();
    if (!canvas) { rafId = null; return; }
    draw(canvas, (now % TICK_MS) / TICK_MS);
    if (buf.length > 0 && !reducedMotion()) rafId = requestAnimationFrame(tick);
    else rafId = null;
  }

  function renderChart(snap) {
    if (!snap || typeof snap.total_equity !== 'number' || !isFinite(snap.total_equity)) return;
    buf.push(snap.total_equity);
    if (buf.length > CAP) buf.shift();
    var canvas = canvasEl();
    if (!canvas) return;
    if (rafId === null) rafId = requestAnimationFrame(tick);
    else draw(canvas, 0);
  }

  function renderBrief(snap) {
    var list = field('signal-brief');
    var empty = field('brief-empty');
    if (!list || !empty) return;
    var signals = Array.isArray(snap.signals) ? snap.signals : [];
    var latest = signals.slice(-BRIEF_LEN).reverse();

    list.innerHTML = '';
    empty.hidden = latest.length > 0;
    list.hidden = latest.length === 0;

    latest.forEach(function (sig, i) {
      var k = sigKind(sig.signal);
      var li = document.createElement('li');
      li.className = 'brief-item pop-in';
      li.style.animationDelay = (i * 70) + 'ms';

      var badge = document.createElement('span');
      badge.className = 'badge ' + k.cls;
      badge.textContent = k.text;

      var symbol = document.createElement('span');
      symbol.className = 'brief-symbol num';
      symbol.textContent = sig.symbol || '------';

      var strength = document.createElement('span');
      strength.className = 'brief-strength num dim';
      strength.textContent = typeof sig.strength === 'number' ? sig.strength.toFixed(2) : '--';

      var time = document.createElement('span');
      time.className = 'brief-time num dim';
      time.textContent = (sig.timestamp || '').split(' ').pop() || '--:--:--';

      li.appendChild(badge);
      li.appendChild(symbol);
      li.appendChild(strength);
      li.appendChild(time);
      list.appendChild(li);
    });
  }

  function onSnapshot(snap) {
    if (!root || !snap) return;
    renderStats(snap);
    renderChart(snap);
    renderBrief(snap);
  }

  App.pages.dashboard = {
    mount: function (rootEl) {
      root = rootEl;
      offSnapshot = App.bus.on('snapshot', onSnapshot);
      var canvas = canvasEl();
      if (canvas) {
        if (buf.length > 0) draw(canvas, 0);
        else { var s = setup(canvas); if (s) drawEmpty(s.ctx, s.w, s.h); }
        if (buf.length > 0 && !reducedMotion() && rafId === null) rafId = requestAnimationFrame(tick);
        if (typeof ResizeObserver === 'function') {
          this._ro = new ResizeObserver(function () { draw(canvas, 0); });
          this._ro.observe(canvas);
        }
      }
    },
    unmount: function () {
      if (offSnapshot) { offSnapshot(); offSnapshot = null; }
      if (rafId !== null) { cancelAnimationFrame(rafId); rafId = null; }
      if (this._ro) { this._ro.disconnect(); this._ro = null; }
      root = null;
    },
    _ro: null
  };
})();