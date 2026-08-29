/**
 * @file page_agent.js
 * @brief Agent 思考页，挂在 window.App.pages.agent
 *
 * 层级：
 *   frontend/src/dom/
 *
 * 项目内绝对路径：
 *   frontend/src/dom/page_agent.js
 *
 * 模块作用：
 *   渲染 snapshot.thinking 中各股票的思考时间线，
 *   包括状态事件、工具芯片、打字机文本和最终决策盖章。
 *
 * 使用者：
 *   router.js 在 #/agent 页面挂载时调用 mount()。
 *
 * 项目角色：
 *   前端 Agent 页专属渲染模块，订阅 snapshot 事件驱动 UI 更新。
 *
 * 引入说明：
 *   依赖全局事件总线 App.bus。
 *   使用 index.html 中的 tpl-think-card 模板。
 *
 * 维护记录：
 *   2026-08-29 初始创建
 */

(function () {
  'use strict';

  window.App = window.App || {};
  var App = window.App;
  App.pages = App.pages || {};

  var TYPE_MS = 40;

  var root = null;
  var offSnapshot = null;
  var lastThinking = null;
  var typedProgress = {};
  var cards = {};

  function field(name) { return root && root.querySelector('[data-f="' + name + '"]'); }

  function reducedMotion() {
    return !!(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches);
  }

  function decisionKind(d) {
    if (d === 'buy' || d === 'cover') return { cls: 'stamp-buy', text: '买入' };
    if (d === 'sell' || d === 'short') return { cls: 'stamp-sell', text: '卖出' };
    return { cls: 'stamp-hold', text: '持有' };
  }

  function createCard(symbol) {
    var tpl = document.getElementById('tpl-think-card');
    var grid = field('think-grid');
    if (!tpl || !grid) return null;

    var el = tpl.content.firstElementChild.cloneNode(true);
    function f(name) { return el.querySelector('[data-f="' + name + '"]'); }
    f('symbol').textContent = symbol || '------';

    grid.appendChild(el);

    var st = {
      el: el,
      timeline: f('timeline'),
      renderedCount: 0,
      textLi: null,
      textSpan: null,
      fullText: '',
      timer: null
    };
    cards[symbol] = st;
    return st;
  }

  function appendEvent(st, ev) {
    var li = document.createElement('li');

    if (ev.type === 'tool' || ev.type === 'tool_result') {
      li.className = 'think-event ' + (ev.type === 'tool' ? 'ev-tool' : 'ev-tool_result');
      var chip = document.createElement('span');
      chip.className = 'tool-chip';
      chip.setAttribute('data-state', ev.type === 'tool' ? 'running' : 'done');
      chip.textContent = ev.name || ev.text || 'tool';
      li.appendChild(chip);
      if (ev.type === 'tool_result' && ev.text) {
        var res = document.createElement('span');
        res.className = 'tool-result-text dim';
        res.textContent = ev.text;
        li.appendChild(res);
      }
    } else {
      li.className = 'think-event ev-status';
      var txt = document.createElement('span');
      txt.textContent = ev.text || '';
      li.appendChild(txt);
    }

    if (ev.t) {
      var time = document.createElement('span');
      time.className = 'ev-time num dim';
      time.textContent = ev.t;
      li.appendChild(time);
    }

    st.timeline.appendChild(li);
  }

  function ensureTextLi(st, ev) {
    if (st.textLi) return st.textSpan;
    var li = document.createElement('li');
    li.className = 'think-event ev-text';
    var span = document.createElement('span');
    span.className = 'ev-text-body';
    li.appendChild(span);
    if (ev && ev.t) {
      var time = document.createElement('span');
      time.className = 'ev-time num dim';
      time.textContent = ev.t;
      li.appendChild(time);
    }
    st.timeline.appendChild(li);
    st.textLi = li;
    st.textSpan = span;
    return span;
  }

  function startTypewriter(symbol, st) {
    if (st.timer || reducedMotion()) {
      if (reducedMotion() && st.textSpan) {
        st.textSpan.textContent = st.fullText;
        typedProgress[symbol] = st.fullText.length;
      }
      return;
    }
    st.timer = setInterval(function () {
      var typed = typedProgress[symbol] || 0;
      if (!st.textSpan || typed >= st.fullText.length) {
        clearInterval(st.timer);
        st.timer = null;
        return;
      }
      typed += 1;
      typedProgress[symbol] = typed;
      st.textSpan.textContent = st.fullText.slice(0, typed);
      if (typed >= st.fullText.length) {
        clearInterval(st.timer);
        st.timer = null;
      }
    }, TYPE_MS);
  }

  function renderCard(symbol, data) {
    var st = cards[symbol] || createCard(symbol);
    if (!st) return;

    var events = Array.isArray(data.events) ? data.events : [];

    if (events.length < st.renderedCount) {
      st.timeline.innerHTML = '';
      st.renderedCount = 0;
      st.textLi = st.textSpan = null;
      st.fullText = '';
      typedProgress[symbol] = 0;
      if (st.timer) { clearInterval(st.timer); st.timer = null; }
      var oldFinal = st.el.querySelector('[data-f="final"]');
      if (oldFinal) oldFinal.hidden = true;
    }

    for (var i = st.renderedCount; i < events.length; i++) {
      var ev = events[i] || {};
      if (ev.type === 'text') {
        ensureTextLi(st, ev);
        st.fullText = ev.text || '';
      } else {
        appendEvent(st, ev);
      }
      st.renderedCount = i + 1;
    }

    for (var j = events.length - 1; j >= 0; j--) {
      if (events[j] && events[j].type === 'text' && typeof events[j].text === 'string') {
        if (events[j].text.length > st.fullText.length) st.fullText = events[j].text;
        break;
      }
    }
    if (st.textSpan) startTypewriter(symbol, st);

    var stateEl = st.el.querySelector('[data-f="state"]');
    if (stateEl) {
      stateEl.textContent = data.state === 'done' ? '已完成' : '思考中';
      stateEl.setAttribute('data-state', data.state || 'thinking');
    }

    var finalBox = st.el.querySelector('[data-f="final"]');
    if (data.state === 'done' && data.final && finalBox) {
      var k = decisionKind(data.final.final_decision);
      var stamp = finalBox.querySelector('[data-f="decision"]');
      if (stamp) stamp.textContent = k.text;
      finalBox.classList.remove('stamp-buy', 'stamp-sell', 'stamp-hold');
      finalBox.classList.add(k.cls);
      var conf = finalBox.querySelector('[data-f="confidence"]');
      if (conf) {
        conf.textContent = typeof data.final.confidence === 'number'
          ? Math.round(data.final.confidence * 100) + '%' : '--';
      }
      var reason = finalBox.querySelector('[data-f="reason"]');
      if (reason) reason.textContent = data.final.reason || '';
      finalBox.hidden = false;
    }
  }

  function render(thinking) {
    if (!root) return;
    var empty = field('agent-empty');
    var grid = field('think-grid');
    if (!empty || !grid) return;

    var list = Array.isArray(thinking) ? thinking : [];
    empty.hidden = list.length > 0;
    grid.hidden = list.length === 0;

    var alive = {};
    list.forEach(function (t) { if (t && t.symbol) alive[t.symbol] = true; });
    Object.keys(cards).forEach(function (symbol) {
      if (!alive[symbol]) {
        var st = cards[symbol];
        if (st.timer) clearInterval(st.timer);
        st.el.remove();
        delete cards[symbol];
      }
    });

    list.forEach(function (t) {
      if (t && t.symbol) renderCard(t.symbol, t);
    });
  }

  function onSnapshot(snap) {
    if (!snap) return;
    if (snap.thinking) lastThinking = snap.thinking;
    render(lastThinking);
  }

  App.pages.agent = {
    mount: function (rootEl) {
      root = rootEl;
      cards = {};
      offSnapshot = App.bus.on('snapshot', onSnapshot);
      render(lastThinking);
    },
    unmount: function () {
      if (offSnapshot) { offSnapshot(); offSnapshot = null; }
      Object.keys(cards).forEach(function (symbol) {
        if (cards[symbol].timer) clearInterval(cards[symbol].timer);
      });
      cards = {};
      root = null;
    }
  };
})();