// App shell: connection, live polling, tab and page navigation, commands.
(function () {
  const TABS = [
    { id: 'home', t: '总览', ic: 'home' },
    { id: 'measure', t: '测量', ic: 'pulse' },
    { id: 'records', t: '记录', ic: 'list' },
    { id: 'points', t: '测点', ic: 'pin' },
    { id: 'device', t: '设备', ic: 'device' },
  ];
  const App = {
    version: '1.0.5',
    pages: {},
    dev: null,
    live: null,
    online: false,
    fails: 0,
    tab: 'home',
    stack: [],
    views: {},
    records: null,
    recordsAt: 0,
    settings: null,
    points: null,
    lastResultId: null,
    unseenAlarm: false,
    listeners: [],
  };

  App.register = (name, page) => { App.pages[name] = page; };
  App.on = (fn) => { App.listeners.push(fn); return () => { App.listeners = App.listeners.filter((f) => f !== fn); }; };

  // ------------------------------------------------------------- theme
  App.applyTheme = function () {
    const pref = U.store.get('theme', 'light');
    const dark = pref === 'auto' ? !(window.matchMedia && matchMedia('(prefers-color-scheme: light)').matches) : pref === 'dark';
    document.documentElement.setAttribute('data-theme', dark ? 'dark' : 'light');
    try { if (window.VibmonNative && VibmonNative.setTheme) VibmonNative.setTheme(dark); } catch (e) { /* none */ }
    Object.values(App.pages).forEach((p) => p.onTheme && p.onTheme());
  };

  // ------------------------------------------------------------- connection
  App.connect = async function (entry, quiet) {
    App.disconnect(true);
    App.dev = entry.kind === 'demo' ? new DemoDevice() : new Api.Device(entry);
    App.live = null; App.records = null; App.settings = null; App.points = null; App.lastResultId = null;
    const r = await App.dev.live();
    if (!r || !r.ok) {
      if (!quiet) U.toast(U.errText(r && r.error), 'err');
      if (!quiet || entry.kind === 'demo') { App.dev = null; return false; }
    }
    if (entry.kind !== 'demo') {
      const saved = U.store.get('devices', []).filter((d) => !(d.host === entry.host && d.port === entry.port));
      saved.unshift({ kind: entry.kind, host: entry.host, port: entry.port, name: (r && r.data && r.data.dev) || entry.name, id: entry.id, at: Date.now() });
      U.store.set('devices', saved.slice(0, 8));
    }
    U.store.set('last', entry);
    if (r && r.ok) App.ingest(r.data);
    App.showShell();
    App.poll();
    App.loadRecords();
    App.loadSettings();
    // The device clock follows the phone when it has no NTP time yet.
    if (r && r.ok && !r.data.tv && entry.kind !== 'demo' && U.store.get('autoTime', true)) {
      App.dev.command('rtc_set', { unix_ms: Date.now() }).then((x) => { if (x && x.ok) U.toast('已用手机时间校准设备时钟'); });
    }
    return true;
  };
  App.disconnect = function (silent) {
    clearTimeout(App.pollTimer);
    App.pollTimer = 0;
    App.dev = null;
    App.online = false;
    if (!silent) {
      U.store.set('last', null);
      App.showConnect();
    }
  };

  App.poll = async function () {
    clearTimeout(App.pollTimer);
    const dev = App.dev;
    if (!dev) return;
    const r = await dev.live();
    if (dev !== App.dev) return;
    if (r && r.ok) {
      App.fails = 0;
      App.ingest(r.data);
    } else {
      App.fails++;
      if (App.fails >= 3 && App.online) { App.online = false; App.emit(); }
    }
    const busy = App.live && (App.live.ms === 'capturing' || App.live.ms === 'analyzing');
    App.pollTimer = setTimeout(App.poll, document.hidden ? 4000 : busy ? 450 : App.fails ? 2500 : 1000);
  };
  App.ingest = function (live) {
    const prev = App.live;
    App.live = live;
    App.online = !live.gw || live.gw.ax_online !== false;
    const id = live.last && live.last.id;
    if (id && App.lastResultId && id !== App.lastResultId) App.onNewResult(live.last);
    if (id) App.lastResultId = id;
    if (prev && prev.ms === 'capturing' && live.ms !== 'capturing') U.haptic(20);
    const cap = live.ms === 'capturing' || live.ms === 'analyzing';
    if (!!(prev && (prev.ms === 'capturing' || prev.ms === 'analyzing')) !== cap) {
      try { if (window.VibmonNative && VibmonNative.keepScreenOn) VibmonNative.keepScreenOn(cap); } catch (e) { /* none */ }
    }
    App.emit();
  };
  App.emit = function () {
    App.listeners.forEach((f) => { try { f(App.live); } catch (e) { console.error(e); } });
    App.renderStatus();
  };
  App.onNewResult = function (last) {
    App.loadRecords(true);
    const l = U.level(last.lv);
    if (last.lv === 'alarm' || last.lv === 'warn') {
      U.haptic(last.lv === 'alarm' ? 300 : 120);
      U.toast('振动' + l.t + '：峰峰值 ' + U.num(last.pp) + ' ' + last.u, last.lv === 'alarm' ? 'err' : 'warn');
      if (App.tab !== 'records') App.unseenAlarm = true;
    } else if (last.ok) {
      U.toast('测量完成 · ' + U.hz(last.hz) + ' Hz');
    } else {
      U.toast('测量未通过质量检查', 'warn');
    }
    App.renderTabs();
  };
  App.loadRecords = async function (force) {
    if (!App.dev) return null;
    if (!force && App.records && Date.now() - App.recordsAt < 15000) return App.records;
    const r = await App.dev.records(300);
    if (r && r.ok) { App.records = r.data || []; App.recordsAt = Date.now(); App.emitRecords(); }
    return App.records;
  };
  App.emitRecords = function () { Object.values(App.pages).forEach((p) => p.onRecords && p.onRecords(App.records)); };
  App.loadSettings = async function () {
    if (!App.dev) return null;
    const [s, p] = await Promise.all([App.dev.settings(), App.dev.points()]);
    if (s && s.ok) App.settings = s.data;
    if (p && p.ok) App.points = p.data || [];
    Object.values(App.pages).forEach((pg) => pg.onSettings && pg.onSettings());
    return App.settings;
  };

  // Runs a command with feedback; refreshes the live state right after.
  App.cmd = async function (cmd, args, okText) {
    if (!App.dev) return { ok: false };
    U.haptic(10);
    const r = await App.dev.command(cmd, args);
    if (r && r.ok) { if (okText) U.toast(okText); } else U.toast(U.errText(r && r.error), 'err');
    App.poll();
    return r || { ok: false };
  };

  // ------------------------------------------------------------- shell
  App.showConnect = function () {
    U.$('#tabbar').classList.add('hide');
    Object.values(App.views).forEach((v) => v.classList.add('hidden'));
    App.stack.forEach((s) => s.el.remove()); App.stack = [];
    let v = U.$('#view-connect');
    if (!v) {
      v = document.createElement('div'); v.className = 'view'; v.id = 'view-connect';
      U.$('#app').appendChild(v);
    }
    v.classList.remove('hidden');
    App.pages.connect.render(v);
  };
  App.showShell = function () {
    const c = U.$('#view-connect'); if (c) c.classList.add('hidden');
    if (App.pages.connect.hide) App.pages.connect.hide();
    TABS.forEach((t) => {
      if (!App.views[t.id]) {
        const v = document.createElement('div'); v.className = 'view hidden'; v.id = 'view-' + t.id;
        U.$('#app').appendChild(v);
        App.views[t.id] = v;
        App.pages[t.id].render(v);
      } else if (App.pages[t.id].reset) {
        App.pages[t.id].reset();
      }
    });
    U.$('#tabbar').classList.remove('hide');
    App.go(App.tab || 'home', true);
  };
  App.renderTabs = function () {
    const bar = U.$('#tabbar');
    if (!bar.querySelector('.tab')) {
      bar.innerHTML = '<i class="ind"></i>' + TABS.map((t) => '<button class="tab" data-tab="' + t.id + '">' + Icon(t.ic) + '<span>' + t.t + '</span></button>').join('');
    }
    U.$$('.tab', bar).forEach((b) => {
      const id = b.getAttribute('data-tab');
      b.classList.toggle('active', id === App.tab);
      const dot = b.querySelector('.dot');
      const want = id === 'records' && App.unseenAlarm;
      if (want && !dot) b.insertAdjacentHTML('beforeend', '<i class="dot"></i>');
      if (!want && dot) dot.remove();
    });
    App.moveIndicator(TABS.findIndex((t) => t.id === App.tab));
  };
  App.moveIndicator = function (pos) {
    const bar = U.$('#tabbar'), ind = bar && bar.querySelector('.ind');
    if (!ind) return;
    const w = (bar.clientWidth - 14) / TABS.length;
    ind.style.width = w + 'px';
    ind.style.transform = 'translateX(' + (pos * w) + 'px)';
  };
  const idx = (t) => TABS.findIndex((x) => x.id === t);
  // Tab switch: the new page slides in from the side of its tab, the old one
  // drifts the other way and fades.
  App.go = function (tab, instant) {
    while (App.stack.length) App.back(true);
    const prev = App.tab;
    App.tab = tab;
    if (tab === 'records') App.unseenAlarm = false;
    const nv = App.views[tab], ov = App.views[prev];
    const dir = Math.sign(idx(tab) - idx(prev)) || 1;
    Object.keys(App.views).forEach((k) => { if (k !== tab && k !== prev) App.views[k].classList.add('hidden'); });
    if (instant || prev === tab || !ov) {
      if (ov && ov !== nv) ov.classList.add('hidden');
      nv.classList.remove('hidden', 'anim'); nv.style.transform = ''; nv.style.opacity = '';
    } else {
      nv.classList.remove('hidden', 'anim');
      if (!App.dragging) { nv.style.transform = 'translateX(' + (dir * 36) + '%)'; nv.style.opacity = '0'; }
      void nv.offsetWidth;
      nv.classList.add('anim'); ov.classList.add('anim');
      nv.style.transform = ''; nv.style.opacity = '';
      ov.style.transform = 'translateX(' + (-dir * 24) + '%)'; ov.style.opacity = '0';
      clearTimeout(App.goTimer);
      App.goTimer = setTimeout(() => {
        if (App.tab !== prev) { ov.classList.add('hidden'); }
        ov.classList.remove('anim'); ov.style.transform = ''; ov.style.opacity = '';
        nv.classList.remove('anim');
      }, 560);
      if (!nv.dataset.revealed) { nv.dataset.revealed = 1; nv.classList.add('reveal'); setTimeout(() => nv.classList.remove('reveal'), 1200); }
    }
    Object.keys(App.views).forEach((k) => { const p = App.pages[k]; if (k === tab) { p.show && p.show(); } else if (k === prev && prev !== tab) { p.hide && p.hide(); } });
    App.renderTabs();
  };
  // Detail page: slides in from the right over a receding page.
  App.push = function (name, params) {
    const page = Object.create(App.pages[name]);
    const el = document.createElement('div'); el.className = 'view push enter-right';
    U.$('#app').appendChild(el);
    const under = App.stack.length ? App.stack[App.stack.length - 1].el : App.views[App.tab];
    page.render(el, params);
    App.stack.push({ el, page, under });
    U.$('#tabbar').classList.add('hide');
    void el.offsetWidth;
    el.classList.add('anim'); if (under) under.classList.add('anim');
    el.classList.remove('enter-right'); if (under) under.classList.add('leave-left');
    if (page.show) page.show();
    return page;
  };
  App.back = function (instant) {
    if (U.sheet.current) { U.sheet.current(null); return true; }
    const top = App.stack.pop();
    if (!top) return false;
    if (top.page.destroy) top.page.destroy();
    if (top.under) { top.under.classList.add('anim'); top.under.style.transform = ''; top.under.classList.remove('leave-left'); }
    if (instant) top.el.remove();
    else { top.el.classList.add('anim'); top.el.style.transform = ''; top.el.classList.add('enter-right'); setTimeout(() => top.el.remove(), 560); }
    if (!App.stack.length && App.dev) U.$('#tabbar').classList.remove('hide');
    return true;
  };

  // Gestures: horizontal swipe on a tab page follows the finger and switches
  // to the neighbouring tab; a swipe from the left edge closes a detail page.
  App.gestures = function () {
    let g = null;
    const noSwipe = (t) => t.closest('canvas, input, .chips, .seg, .sheet, .preview, .no-swipe');
    document.addEventListener('touchstart', (e) => {
      if (e.touches.length !== 1 || U.sheet.current) return;
      const t = e.touches[0];
      const top = App.stack[App.stack.length - 1];
      if (top) { if (t.clientX < 28) g = { kind: 'back', x: t.clientX, y: t.clientY, el: top.el, under: top.under, t: Date.now() }; return; }
      if (!App.dev || noSwipe(e.target)) return;
      g = { kind: 'tab', x: t.clientX, y: t.clientY, t: Date.now(), on: false };
    }, { passive: true });
    document.addEventListener('touchmove', (e) => {
      if (!g) return;
      const t = e.touches[0];
      const dx = t.clientX - g.x, dy = t.clientY - g.y;
      const W = window.innerWidth;
      if (!g.on) {
        if (Math.abs(dx) < 10) return;
        if (Math.abs(dy) > Math.abs(dx) * 0.8) { g = null; return; }
        g.on = true;
        if (g.kind === 'tab') {
          const i = idx(App.tab);
          g.cur = App.views[App.tab];
          g.dir = dx < 0 ? 1 : -1;
          g.nextTab = TABS[i + g.dir] ? TABS[i + g.dir].id : null;
          g.next = g.nextTab ? App.views[g.nextTab] : null;
          if (g.next) { g.next.classList.remove('hidden', 'anim'); }
          g.cur.classList.remove('anim');
        } else { g.el.classList.remove('anim'); if (g.under) g.under.classList.remove('anim'); }
      }
      if (g.kind === 'tab') {
        const lim = g.next ? dx : dx * 0.25;  // rubber band at the ends
        g.cur.style.transform = 'translateX(' + lim + 'px)';
        g.cur.style.opacity = String(1 - Math.min(0.5, Math.abs(lim) / W));
        if (g.next) { g.next.style.transform = 'translateX(' + (g.dir * W * 0.4 + dx * 0.4) + 'px)'; g.next.style.opacity = String(Math.min(1, Math.abs(dx) / (W * 0.6))); }
        App.moveIndicator(idx(App.tab) - dx / W);
      } else {
        const x = Math.max(0, dx);
        g.el.style.transform = 'translateX(' + x + 'px)';
        if (g.under) g.under.style.transform = 'translateX(' + (-26 + 26 * x / W) + '%)';
      }
    }, { passive: true });
    document.addEventListener('touchend', (e) => {
      if (!g || !g.on) { g = null; return; }
      const dx = (e.changedTouches[0].clientX - g.x), W = window.innerWidth;
      const fast = Math.abs(dx) / Math.max(1, Date.now() - g.t) > 0.5;
      if (g.kind === 'tab') {
        if (g.next && (Math.abs(dx) > W * 0.28 || fast)) {
          App.dragging = true; U.haptic(6);
          App.go(g.nextTab);
          App.dragging = false;
        } else {
          [g.cur, g.next].forEach((v) => { if (v) { v.classList.add('anim'); v.style.transform = ''; v.style.opacity = ''; } });
          if (g.next) { const nx = g.next; nx.style.transform = 'translateX(' + (g.dir * 36) + '%)'; nx.style.opacity = '0'; setTimeout(() => { if (App.tab !== g.nextTab) nx.classList.add('hidden'); nx.style.transform = ''; nx.style.opacity = ''; }, 560); }
          App.moveIndicator(idx(App.tab));
        }
      } else {
        g.el.classList.add('anim'); if (g.under) g.under.classList.add('anim');
        if (dx > W * 0.33 || fast) { g.el.style.transform = ''; App.back(); }
        else { g.el.style.transform = ''; if (g.under) g.under.style.transform = ''; }
      }
      g = null;
    });
  };

  // Android back key (MainActivity calls it; false -> leave the app).
  window.__jwBack = function () {
    if (App.back()) return true;
    if (App.dev && App.tab !== 'home') { App.go('home'); return true; }
    return false;
  };

  // Header fragment used by the tab pages.
  App.header = function (title, right) {
    return '<div class="topbar"><h1>' + title + '</h1>' + (right || '') + '</div>';
  };
  App.pushHeader = function (title, right) {
    return '<div class="topbar"><button class="icon-btn" data-act="back">' + Icon('back') + '</button><h2>' + U.esc(title) + '</h2>' + (right || '<span style="width:40px"></span>') + '</div>';
  };
  App.renderStatus = function () {
    U.$$('[data-conn]').forEach((el) => {
      const d = App.dev;
      if (!d) return;
      const kind = d.kind === 'demo' ? '演示' : d.lan ? '局域网' : 'Wi-Fi 网关';
      const cls = App.online ? 'ok' : 'warn';
      el.innerHTML = '<span class="pill ' + cls + '"><span class="led' + (App.online ? ' live' : '') + '"></span>' + (App.online ? kind : '重连中') + '</span>';
    });
  };

  // ------------------------------------------------------------- start
  App.start = async function () {
    App.applyTheme();
    if (window.matchMedia) matchMedia('(prefers-color-scheme: light)').addEventListener('change', () => App.applyTheme());
    document.addEventListener('click', (e) => {
      const t = e.target.closest('[data-tab]');
      if (t) { U.haptic(6); App.go(t.getAttribute('data-tab')); return; }
      const b = e.target.closest('[data-act="back"]');
      if (b) App.back();
    });
    App.gestures();
    window.addEventListener('resize', () => App.moveIndicator(idx(App.tab)));
    document.addEventListener('visibilitychange', () => { if (!document.hidden && App.dev) App.poll(); });
    App.renderTabs();
    const last = U.store.get('last', null);
    const splash = U.$('#splash');
    // #shot=<tab>: demo device on that tab (build-machine screenshots).
    const shot = /[#&]shot=(\w+)/.exec(location.hash);
    if (shot) {
      await App.connect({ kind: 'demo', host: 'demo', port: 0, name: '演示设备' }, true);
      App.go(shot[1], true);
      if (shot[1] === 'device') setTimeout(() => { const t = App.pages.device.twin; if (t) { t.setExplode(1); t.auto = false; t.ry = -0.95; t.rx = -0.42; } }, 300);
    } else if (last && (await App.connect(last, true))) { /* reconnected */ } else App.showConnect();
    if (splash) { splash.classList.add('gone'); setTimeout(() => splash.remove(), 600); }
  };
  window.App = App;
  window.addEventListener('DOMContentLoaded', () => App.start());
})();
