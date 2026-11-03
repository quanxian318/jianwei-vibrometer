// 测点: NFC measuring points (configured + seen in records), the current point,
// per-point trend and history; manual point entry.
(function () {
  function collect() {
    const map = {};
    (App.points || []).forEach((p) => { map[p.id] = { id: p.id, name: p.name, uid: p.uid, recs: [] }; });
    (App.records || []).forEach((r) => {
      if (!r.pt) return;
      if (!map[r.pt]) map[r.pt] = { id: r.pt, name: r.pn || '', uid: '', recs: [] };
      if (!map[r.pt].name && r.pn) map[r.pt].name = r.pn;
      map[r.pt].recs.push(r);
    });
    return Object.values(map).sort((a, b) => a.id.localeCompare(b.id));
  }
  const worst = (recs) => {
    const r = recs.find((x) => x.ok);
    return r ? r.lv : 'unknown';
  };

  const List = {
    render(el) {
      this.el = el;
      el.innerHTML = App.header('测点', '<button class="icon-btn" id="pAdd">' + Icon('plus') + '</button>') +
        '<div class="card" id="pCur" data-tilt></div>' +
        '<div class="grid3" id="pSum"></div>' +
        '<div class="section-title">全部测点</div><div id="pList"></div>';
      U.$('#pAdd', el).onclick = () => this.manual();
      U.$('#pList', el).addEventListener('click', (e) => { const r = e.target.closest('[data-pid]'); if (r) App.push('point', { id: r.getAttribute('data-pid') }); });
      U.$('#pCur', el).addEventListener('click', (e) => { if (e.target.closest('[data-pick]')) this.manual(); });
      App.on(() => this.drawCur());
      this.draw();
    },
    reset() { this.draw(); },
    onRecords() { this.draw(); },
    onSettings() { this.draw(); },
    drawCur() {
      const l = App.live || {};
      const pt = l.pt || {};
      const html = '<div class="flex"><div class="ic" style="width:48px;height:48px;border-radius:16px;background:var(--grad);color:var(--accent-ink);display:flex;align-items:center;justify-content:center">' + Icon('nfc') + '</div>' +
        '<div class="grow"><div class="muted" style="font-size:12px">当前测点</div><div style="font-size:19px;font-weight:700">' + U.esc(pt.id || '未选择') + '</div>' +
        '<div class="muted" style="font-size:12.5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">' + U.esc(pt.name || (pt.uid ? 'UID ' + pt.uid : '刷 NFC 标签选择测点')) + '</div></div>' +
        '<button class="btn small" data-pick>切换</button></div>';
      const c = U.$('#pCur', this.el);
      if (c && c.innerHTML !== html) c.innerHTML = html;
    },
    draw() {
      const el = this.el;
      if (!el) return;
      this.drawCur();
      const pts = collect();
      const n = { normal: 0, warn: 0, alarm: 0 };
      pts.forEach((p) => { const w = worst(p.recs); if (n[w] != null) n[w]++; });
      U.$('#pSum', el).innerHTML = [['normal', '正常'], ['warn', '预警'], ['alarm', '报警']].map((k) =>
        '<div class="stat" style="text-align:center"><div class="v num c-' + U.level(k[0]).c + '" style="font-size:26px">' + n[k[0]] + '</div><div class="k" style="justify-content:center">' + k[1] + '测点</div></div>').join('');
      if (!pts.length) {
        U.$('#pList', el).innerHTML = '<div class="empty-state">' + Icon('pin') + '<h4>暂无测点</h4><div>刷 NFC 标签或点右上角手动添加</div></div>';
        return;
      }
      U.$('#pList', el).innerHTML = '<div class="list stagger">' + pts.map((p) => {
        const w = worst(p.recs), L = U.level(w);
        const series = p.recs.filter((r) => r.ok && r.pp != null).slice(0, 12).reverse().map((r) => r.pp);
        const last = p.recs[0];
        return '<div class="row" data-pid="' + U.esc(p.id) + '"><div class="ic" style="background:var(--' + L.c + '-soft);color:var(--' + L.c + ')">' + Icon('target') + '</div>' +
          '<div class="main"><div class="title">' + U.esc(p.id) + (p.name ? ' · ' + U.esc(p.name) : '') + '</div><div class="desc">' +
          (last ? U.timeText(last.t) + ' · ' + p.recs.length + ' 次' : '暂无测量') + '</div></div>' +
          '<div class="end">' + Charts.spark(series, Charts.css('--' + L.c), 64, 26) + Icon('chev', 'chev') + '</div></div>';
      }).join('') + '</div>';
    },
    manual() {
      const pts = collect();
      U.sheet('<h2>选择测点</h2><div class="list" style="max-height:38vh;overflow:auto">' +
        pts.map((p) => '<div class="row" data-close="' + U.esc(p.id) + '"><div class="ic accent">' + Icon('target') + '</div><div class="main"><div class="title">' + U.esc(p.id) + '</div><div class="desc">' + U.esc(p.name || '') + '</div></div></div>').join('') +
        '</div><div class="section-title" style="margin-top:6px">新建测点</div>' +
        '<label class="field"><span>编号</span><input class="input" id="nId" placeholder="例如 P-05" autocomplete="off"></label>' +
        '<label class="field"><span>名称</span><input class="input" id="nName" placeholder="例如 3# 水泵 驱动端" autocomplete="off"></label>' +
        '<div class="actions"><button class="btn" data-close="">取消</button><button class="btn primary" id="nGo">设为当前测点</button></div>', (sh, close) => {
        U.$('#nGo', sh).onclick = () => {
          const id = U.$('#nId', sh).value.trim();
          if (!id) { U.toast('请输入测点编号', 'warn'); return; }
          close({ id, name: U.$('#nName', sh).value.trim() });
        };
      }).then((v) => {
        if (!v || v === true) return;
        if (typeof v === 'string') { const p = pts.find((x) => x.id === v); App.cmd('set_point', { id: v, name: p ? p.name : '' }, '当前测点：' + v); }
        else App.cmd('set_point', v, '当前测点：' + v.id);
      });
    },
  };
  App.register('points', List);

  const Detail = {
    render(el, params) {
      this.el = el;
      const p = collect().find((x) => x.id === params.id) || { id: params.id, name: '', recs: [] };
      const recs = p.recs;
      const ok = recs.filter((r) => r.ok && r.pp != null);
      const L = U.level(worst(recs));
      const avgHz = ok.length ? ok.reduce((a, r) => a + r.hz, 0) / ok.length : null;
      const maxPp = ok.length ? Math.max.apply(null, ok.map((r) => r.pp)) : null;
      el.innerHTML = App.pushHeader(p.id) +
        '<div class="card fade-in" style="background:linear-gradient(160deg,var(--' + L.c + '-soft),var(--surface) 60%)">' +
        '<div class="flex"><div class="grow"><div style="font-size:20px;font-weight:700">' + U.esc(p.name || p.id) + '</div><div class="muted" style="font-size:12.5px">' + (p.uid ? 'NFC ' + U.esc(p.uid) : '手动测点') + '</div></div>' + U.levelPill(worst(recs)) + '</div>' +
        '<div class="grid3 mt16"><div><div class="muted" style="font-size:12px">测量次数</div><div class="num" style="font-size:20px;font-weight:650">' + recs.length + '</div></div>' +
        '<div><div class="muted" style="font-size:12px">平均主频</div><div class="num" style="font-size:20px;font-weight:650">' + (avgHz ? U.hz(avgHz) : '—') + '<small style="font-size:12px;color:var(--text-2)"> Hz</small></div></div>' +
        '<div><div class="muted" style="font-size:12px">最大峰峰值</div><div class="num" style="font-size:20px;font-weight:650">' + (maxPp != null ? U.num(maxPp) : '—') + '</div></div></div></div>' +
        '<div class="card tight"><h3>' + Icon('trend', 's') + '峰峰值趋势</h3><canvas class="chart" id="ptTrend"></canvas></div>' +
        '<div class="card tight"><h3>' + Icon('wave', 's') + '主频变化</h3><canvas class="chart short" id="ptHz"></canvas></div>' +
        '<button class="btn primary block" id="ptUse">' + Icon('target', 's') + '设为当前测点并测量</button>' +
        '<div class="section-title">历史记录</div><div class="list" id="ptList">' + (recs.length ? recs.map(App.pages.home.recRow).join('') : '<div class="row static"><div class="main"><div class="title">暂无记录</div></div></div>') + '</div>';
      const ch = new Charts.Chart(U.$('#ptTrend', el), { padL: 36, yTicks: 3 });
      const ch2 = new Charts.Chart(U.$('#ptHz', el), { padL: 36, yTicks: 2 });
      const tr = ok.slice(0, 30).reverse();
      const set = (App.live && App.live.set) || {};
      ch.set(tr.length >= 2 ? { series: [{ x: tr.map((_, i) => i + 1), y: tr.map((r) => r.pp), dots: true, dotColors: tr.map((r) => Charts.css('--' + U.level(r.lv).c)) }], xInt: true, yMin: 0, yMax: Math.max((set.alarm || 5) * 1.1, maxPp * 1.15), hlines: [{ y: set.warn || 2, color: Charts.css('--warn'), label: '预警' }, { y: set.alarm || 5, color: Charts.css('--alarm'), label: '报警' }] } : { series: [], empty: '记录不足' });
      ch2.set(tr.length >= 2 ? { series: [{ x: tr.map((_, i) => i + 1), y: tr.map((r) => r.hz), dots: true, color: Charts.css('--accent-2') }], xInt: true } : { series: [], empty: '记录不足' });
      U.$('#ptUse', el).onclick = async () => {
        const r = await App.cmd('set_point', { id: p.id, name: p.name || '' });
        if (r.ok) { App.go('measure'); App.pages.measure.start(); }
      };
      U.$('#ptList', el).addEventListener('click', (e) => { const r = e.target.closest('[data-id]'); if (r) App.push('record', { id: r.getAttribute('data-id') }); });
    },
  };
  App.register('point', Detail);
})();
