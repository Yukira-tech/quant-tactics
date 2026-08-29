/**
 * @file page_training.js
 * @brief 训练中心页，挂在 window.App.pages.training
 *
 * 层级：
 *   frontend/src/dom/
 *
 * 项目内绝对路径：
 *   frontend/src/dom/page_training.js
 *
 * 模块作用：
 *   渲染训练进度、指标卡、loss 双曲线和 epoch 历史表。
 *   训练状态缺失或 idle 且无历史时显示空态。
 *
 * 使用者：
 *   router.js 在 #/training 页面挂载时调用 mount()。
 *
 * 项目角色：
 *   前端训练中心页专属渲染模块，订阅 snapshot 事件驱动 UI 更新。
 *
 * 引入说明：
 *   依赖全局事件总线 App.bus。
 *   使用 index.html 中的 tpl-epoch-row 模板与 loss-chart canvas。
 *
 * 维护记录：
 *   2026-08-29 初始创建
 */

(function () {
  'use strict';

  window.App = window.App || {};
  var App = window.App;
  App.pages = App.pages || {};

  var STAGGER_MS = 70;

  var root = null;
  var offSnapshot = null;
  var lastTraining = null;

  var fmtLoss = new Intl.NumberFormat('zh-CN', { minimumFractionDigits: 4, maximumFractionDigits: 4 });

  function cssVar(name, fallback) {
    var v = window.getComputedStyle(document.documentElement).getPropertyValue(name);
    return (v && v.trim()) || fallback;
  }

  function field(name) { return root && root.querySelector('[data-f="' + name + '"]'); }

  function fmtEta(sec) {
    if (typeof sec !== 'number' || !isFinite(sec) || sec < 0) return '--';
    sec = Math.round(sec);
    var m = Math.floor(sec / 60);
    var s = sec % 60;
    return m > 0 ? m + '分' + s + '秒' : s + '秒';
  }

  function pct(part, total) {
    if (!total || total <= 0) return 0;
    return Math.max(0, Math.min(100, part / total * 100));
  }

  function renderProgress(t) {
    var epochBar = field('epoch-bar');
    var batchBar = field('batch-bar');
    if (epochBar) epochBar.style.width = pct(t.epoch, t.total_epochs).toFixed(1) + '%';
    if (batchBar) batchBar.style.width = pct(t.batch, t.total_batches).toFixed(1) + '%';

    var epochText = field('epoch-text');
    if (epochText) epochText.textContent = (t.epoch || 0) + ' / ' + (t.total_epochs || 0);
    var batchText = field('batch-text');
    if (batchText) batchText.textContent = (t.batch || 0) + ' / ' + (t.total_batches || 0);

    var stateEl = field('train-state');
    if (stateEl) {
      var label = { running: '训练中', done: '已完成', idle: '空闲' }[t.state] || '空闲';
      stateEl.textContent = label;
      stateEl.setAttribute('data-state', t.state || 'idle');
    }

    var loss = field('loss');
    if (loss) loss.textContent = typeof t.loss === 'number' ? fmtLoss.format(t.loss) : '--';
    var valLoss = field('val-loss');
    if (valLoss) valLoss.textContent = typeof t.val_loss === 'number' ? fmtLoss.format(t.val_loss) : '--';
    var speed = field('speed');
    if (speed) speed.textContent = typeof t.speed === 'number' ? t.speed.toFixed(1) + ' batch/s' : '--';
    var eta = field('eta');
    if (eta) eta.textContent = t.state === 'done' ? '已完成' : fmtEta(t.eta_seconds);
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
    ctx.fillText('暂无历史 · 首个 epoch 结束后绘制', w / 2, h / 2);
  }

  function drawCurve(ctx, pts, X, Y, color) {
    if (pts.length === 0) return;
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    ctx.beginPath();
    for (var i = 0; i < pts.length; i++) {
      var x = X(pts[i].i), y = Y(pts[i].v);
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.stroke();
    ctx.fillStyle = color;
    for (var j = 0; j < pts.length; j++) {
      ctx.beginPath();
      ctx.arc(X(pts[j].i), Y(pts[j].v), 3, 0, Math.PI * 2);
      ctx.fill();
    }
  }

  function renderChart(t) {
    var canvas = field('loss-chart');
    if (!canvas) return;
    var s = setup(canvas);
    if (!s) return;
    var ctx = s.ctx, w = s.w, h = s.h;

    var history = Array.isArray(t.history) ? t.history : [];
    if (history.length === 0) { drawEmpty(ctx, w, h); return; }

    function seriesOf(key) {
      var pts = [];
      history.forEach(function (e, i) {
        if (e && typeof e[key] === 'number') pts.push({ i: i, v: e[key] });
      });
      return pts;
    }
    var trainPts = seriesOf('train_loss');
    var valPts = seriesOf('val_loss');
    var all = trainPts.concat(valPts).map(function (p) { return p.v; });
    if (all.length === 0) { drawEmpty(ctx, w, h); return; }

    var grid = cssVar('--sakura', '#ffd3e0');
    var cTrain = cssVar('--sakura-deep', '#ff9ebb');
    var cVal = cssVar('--lavender', '#baaaf7');

    var padL = 10, padR = 14, padT = 12, padB = 10;
    var iw = w - padL - padR;
    var ih = h - padT - padB;

    var min = Math.min.apply(null, all);
    var max = Math.max.apply(null, all);
    if (min === max) { var m = Math.abs(min) * 0.05 || 1; min -= m; max += m; }
    else { var pad = (max - min) * 0.08; min -= pad; max += pad; }

    var n = history.length;
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

    drawCurve(ctx, trainPts, X, Y, cTrain);
    drawCurve(ctx, valPts, X, Y, cVal);
  }

  function renderHistory(t) {
    var body = field('epoch-body');
    var empty = field('epoch-empty');
    if (!body || !empty) return;
    var history = Array.isArray(t.history) ? t.history : [];
    var tpl = document.getElementById('tpl-epoch-row');

    body.innerHTML = '';
    empty.hidden = history.length > 0;
    body.parentNode.hidden = history.length === 0;

    history.forEach(function (e, i) {
      if (!tpl) return;
      var row = tpl.content.firstElementChild.cloneNode(true);
      row.style.animationDelay = (i * STAGGER_MS) + 'ms';
      function f(name) { return row.querySelector('[data-f="' + name + '"]'); }

      f('epoch').textContent = e.epoch != null ? e.epoch : '--';
      f('train_loss').textContent = typeof e.train_loss === 'number' ? fmtLoss.format(e.train_loss) : '--';
      f('train_acc').textContent = typeof e.train_acc === 'number' ? (e.train_acc * 100).toFixed(2) + '%' : '--';
      f('val_loss').textContent = typeof e.val_loss === 'number' ? fmtLoss.format(e.val_loss) : '--';
      f('val_acc').textContent = typeof e.val_acc === 'number' ? (e.val_acc * 100).toFixed(2) + '%' : '--';

      body.appendChild(row);
    });
  }

  function render(t) {
    if (!root) return;
    var empty = field('train-empty');
    var body = field('train-body');
    if (!empty || !body) return;

    var hasHistory = t && Array.isArray(t.history) && t.history.length > 0;
    var isEmpty = !t || ((!t.state || t.state === 'idle') && !hasHistory);
    empty.hidden = !isEmpty;
    body.hidden = isEmpty;
    if (isEmpty) return;

    renderProgress(t);
    renderChart(t);
    renderHistory(t);
  }

  function onSnapshot(snap) {
    if (!snap) return;
    if (snap.training) lastTraining = snap.training;
    render(lastTraining);
  }

  App.pages.training = {
    mount: function (rootEl) {
      root = rootEl;
      offSnapshot = App.bus.on('snapshot', onSnapshot);
      render(lastTraining);
    },
    unmount: function () {
      if (offSnapshot) { offSnapshot(); offSnapshot = null; }
      root = null;
    }
  };
})();