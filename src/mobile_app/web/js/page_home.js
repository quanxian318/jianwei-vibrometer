// 总览: vibration state gauge, quick actions, live trace, environment, recent records.
(function () {
  const R = 111, C = 2 * Math.PI * R, SWEEP = 0.75;  // 270 degree arc
  function gauge() {
    return '<div class="gauge"><canvas class="orb" id="hOrb"></canvas><svg viewBox="0 0 236 236"><defs>' +
      '<linearGradient id="gArc" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="var(--accent)"/><stop offset="1" stop-color="var(--accent-2)"/></linearGradient></defs>' +
      '<circle class="track" cx="118" cy="118" r="' + R + '" fill="none" stroke-width="6" stroke-linecap="round" stroke-dasharray="' + C * SWEEP + ' ' + C + '"/>' +
      '<circle class="arc" id="hArc" cx="118" cy="118" r="' + R + '" fill="none" stroke="url(#gArc)" stroke-width="7" stroke-linecap="round" stroke-dasharray="' + C * SWEEP + ' ' + C + '" stroke-dashoffset="' + C * SWEEP + '"/>' +
      '<g id="hTicks"></g></svg>' +
      '<div class="center"><div class="lbl" id="hLbl">峰峰值</div><div class="big num" id="hBig">—</div><div class="unit" id="hUnit">mm</div><div class="lvl" id="hLvl"></div></div></div>';
  }
  const P = {
    render(el) {
      this.el = el;
      el.innerHTML =
        '<div class="dev-head">' + Logo(44) + '<div class="t"><div class="name" id="hName">见微测振仪</div><div class="meta" data-conn></div></div>' +
        '<button class="icon-btn" id="hPoint">' + Icon('pin') + '</button></div>' +
        '<div id="hBanner"></div>' +
        '<div class="card hero">' + gauge() +
        '<div class="hero-row"><div class="it"><div class="v num" id="hHz">—</div><div class="k">主频 Hz</div></div>' +
        '<div class="it"><div class="v num" id="hRms">—</div><div class="k">RMS</div></div>' +
        '<div class="it"><div class="v" id="hPt" style="max-width:110px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">—</div><div class="k">测点</div></div></div></div>' +
        '<div class="quick" id="hQuick">' +
        '<div class="qa" data-q="measure"><div class="ic">' + Icon('play') + '</div>开始测量</div>' +
        '<div class="qa" data-q="light"><div class="ic">' + Icon('light') + '</div>照明</div>' +
        '<div class="qa laser" data-q="laser"><div class="ic">' + Icon('laser') + '</div>十字投影</div>' +
        '<div class="qa" data-q="mute"><div class="ic">' + Icon('mute') + '</div>静音</div></div>' +
        '<div class="section-title">实时位移<span class="right" id="hLiveGo">测量 ›</span></div>' +
        '<div class="card tight"><canvas class="chart short" id="hLive"></canvas></div>' +
        '<div class="section-title">现场环境</div>' +
        '<div class="grid2" id="hEnv"></div>' +
        '<div class="section-title">最近测量<span class="right" data-tab="records">全部 ›</span></div>' +
        '<div class="list" id="hRecent"><div class="row static"><div class="main"><div class="skeleton" style="height:36px"></div></div></div></div>' +
        '<div class="card tight msg" id="hMsg" style="display:none"></div>';
      this.chart = new Charts.Chart(U.$('#hLive', el), { axes: false, grid: true, yTicks: 2 });
      this.orb = new FX3D.Orb(U.$('#hOrb', el));
      U.$('#hQuick', el).addEventListener('click', (e) => {
        const q = e.target.closest('[data-q]');
        if (q) this.quick(q.getAttribute('data-q'));
      });
      U.$('#hLiveGo', el).onclick = () => App.go('measure');
      U.$('#hPoint', el).onclick = () => App.go('points');
      U.$('#hRecent', el).addEventListener('click', (e) => {
        const r = e.target.closest('[data-id]');
        if (r) App.push('record', { id: r.getAttribute('data-id') });
      });
      U.$('#hBanner', el).addEventListener('click', (e) => {
        if (e.target.closest('[data-mute]')) App.cmd('mute', {}, '报警已确认');
      });
      App.on((l) => this.update(l));
      this.onRecords(App.records);
      this.update(App.live);
    },
    reset() { this.onRecords(App.records); this.update(App.live); },
    show() { if (this.orb) this.orb.start(); },
    async quick(q) {
      const l = App.live || {};
      if (q === 'measure') {
        App.go('measure');
        if (l.ms !== 'capturing' && l.ms !== 'analyzing') App.pages.measure.start();
      } else if (q === 'light') {
        App.cmd('light', { on: !(l.light && l.light.on) });
      } else if (q === 'laser') {
        App.cmd('laser', { on: !l.laser });
      } else if (q === 'mute') {
        App.cmd('mute', { on: !l.mute }, l.mute ? '报警声已恢复' : '已静音，报警已确认');
      }
    },
    update(l) {
      const el = this.el;
      if (!el) return;
      U.$('#hName', el).textContent = (App.dev && App.dev.kind === 'demo') ? '演示设备' : (l && l.dev) || (App.dev && App.dev.entry.name) || '见微测振仪';
      if (!l) return;
      const set = l.set || {};
      const alarmPp = set.alarm || 5;
      const capturing = l.ms === 'capturing' || l.ms === 'analyzing';
      const last = l.last;
      const arc = U.$('#hArc', el);
      let frac = 0, color = null;
      if (capturing) {
        frac = l.ms === 'analyzing' ? 1 : U.clamp(l.el / Math.max(0.1, l.dur), 0, 1);
        U.$('#hLbl', el).textContent = l.ms === 'analyzing' ? '分析中' : '采集中';
        U.$('#hBig', el).textContent = l.ms === 'analyzing' ? '···' : l.el.toFixed(1);
        U.$('#hUnit', el).textContent = l.ms === 'analyzing' ? '正在计算频谱' : '/ ' + l.dur + ' s';
        U.$('#hLvl', el).innerHTML = '<span class="pill accent"><span class="led live"></span>测量进行中</span>';
      } else if (last) {
        const lv = U.level(last.lv);
        frac = last.amp === false ? 0 : U.clamp(last.pp / (alarmPp * 1.25), 0.02, 1);
        color = last.u === 'mm' ? Charts.css('--' + lv.c) : null;
        U.$('#hLbl', el).textContent = '峰峰值';
        U.$('#hBig', el).textContent = last.amp === false ? '—' : U.num(last.pp);
        U.$('#hUnit', el).textContent = last.u === 'mm' ? 'mm' : 'px';
        U.$('#hLvl', el).innerHTML = last.ok ? U.levelPill(last.lv) : '<span class="pill warn">未通过质量检查</span>';
      } else {
        U.$('#hLbl', el).textContent = '振动状态';
        U.$('#hBig', el).textContent = '—';
        U.$('#hUnit', el).textContent = '尚无测量';
        U.$('#hLvl', el).innerHTML = '<span class="pill">' + (U.STATE[l.ms] || '') + '</span>';
      }
      arc.style.strokeDashoffset = C * SWEEP * (1 - frac);
      this.orb.set({ hz: last && !capturing ? last.hz : (capturing ? 3 : 0), amp: last && last.amp !== false ? 0.05 + 0.17 * U.clamp(last.pp / alarmPp, 0, 1) : 0.05,
        level: capturing ? 'unknown' : last ? last.lv : 'unknown', active: capturing, progress: capturing ? frac : 0 });
      arc.style.stroke = color || '';
      U.$('#hHz', el).textContent = last && !capturing ? U.hz(last.hz) : '—';
      U.$('#hRms', el).textContent = last && !capturing && last.amp !== false ? U.num(last.rms) : '—';
      U.$('#hPt', el).textContent = (l.pt && (l.pt.id || l.pt.name)) || '未选择';
      // Banner: alarm needing acknowledgement, power, coprocessor.
      let b = '';
      if (l.lv === 'alarm' && !l.mute) b = '<div class="banner alarm">' + Icon('alert') + '<span class="grow">振动超过报警阈值 ' + alarmPp + ' mm</span><button class="btn" data-mute>确认静音</button></div>';
      else if (l.lv === 'warn' && !l.mute) b = '<div class="banner warn">' + Icon('bell') + '<span class="grow">振动达到预警阈值 ' + (set.warn || 2) + ' mm</span><button class="btn" data-mute>确认</button></div>';
      else if (l.pwr && l.pwr.pf) b = '<div class="banner alarm">' + Icon('bolt') + '<span class="grow">掉电预警，设备正在保护数据</span></div>';
      else if (!App.online) b = '<div class="banner warn">' + Icon('unlink') + '<span class="grow">与设备的连接中断，正在重试…</span></div>';
      else if (!l.cam) b = '<div class="banner warn">' + Icon('camera') + '<span class="grow">相机未就绪，设备会自动重试</span></div>';
      const bn = U.$('#hBanner', el);
      if (bn.innerHTML !== b) bn.innerHTML = b;
      // Quick toggles.
      const q = (k) => U.$('[data-q="' + k + '"]', el);
      q('light').classList.toggle('on', !!(l.light && l.light.on));
      q('laser').classList.toggle('on', !!l.laser);
      q('mute').classList.toggle('on', !!l.mute);
      q('measure').classList.toggle('on', capturing);
      q('measure').querySelector('.ic').innerHTML = Icon(capturing ? 'pulse' : 'play');
      q('measure').lastChild.textContent = capturing ? '测量中' : '开始测量';
      // Live trace.
      if (l.wave && l.wave.length) {
        const k = l.mmpx || 1;
        const y = l.wave.slice(-Math.round(2.5 * (l.fps || 45))).map((v) => v * k);
        const mean = y.reduce((a, b) => a + b, 0) / y.length;
        const yy = y.map((v) => v - mean);
        const x = yy.map((_, i) => i);
        this.chart.set({ series: [{ x, y: yy, width: 1.6 }], symmetric: true });
      } else {
        this.chart.set({ series: [], empty: '暂无实时数据' });
      }
      // Environment.
      const env = [
        ['temp', '温度', l.tc != null ? U.num(l.tc, 1) : '—', '°C'],
        ['drop', '湿度', l.rh != null ? U.num(l.rh, 0) : '—', '%'],
        ['ruler', '测距', l.dist != null ? (l.dist / 1000).toFixed(2) : '—', 'm'],
        ['bolt', '整机功率', l.pwr && l.pwr.w ? U.num(l.pwr.w, 1) : '—', 'W'],
      ];
      const envHtml = env.map((e) => '<div class="stat"><div class="k">' + Icon(e[0]) + e[1] + '</div><div class="v num">' + e[2] + '<small>' + e[3] + '</small></div></div>').join('');
      const envEl = U.$('#hEnv', el);
      if (envEl.innerHTML !== envHtml) envEl.innerHTML = envHtml;
      const msg = U.$('#hMsg', el);
      if (l.msg) { msg.style.display = ''; msg.innerHTML = Icon('info', 's') + '<span>' + U.esc(l.msg) + '</span>'; } else msg.style.display = 'none';
    },
    onRecords(recs) {
      const box = this.el && U.$('#hRecent', this.el);
      if (!box || !recs) return;
      if (!recs.length) { box.innerHTML = '<div class="row static"><div class="main"><div class="title">还没有测量记录</div><div class="desc">在「测量」页开始第一次测量</div></div></div>'; return; }
      box.innerHTML = recs.slice(0, 3).map(P.recRow).join('');
    },
    recRow(r) {
      const lv = U.level(r.lv);
      return '<div class="rec" data-id="' + U.esc(r.id) + '"><div class="bar" style="background:var(--' + (r.ok ? lv.c : 'unknown') + ')"></div>' +
        '<div class="main"><div class="l1"><span class="hz num">' + (r.ok ? U.hz(r.hz) : '—') + '<small> Hz</small></span>' + (r.ok ? U.levelPill(r.lv) : '<span class="pill warn">未通过</span>') + '</div>' +
        '<div class="l2">' + U.esc((r.pt ? r.pt + ' · ' : '') + U.timeText(r.t)) + '</div></div>' +
        '<div class="pp"><div class="v num">' + (r.pp != null ? U.num(r.pp) : '—') + '</div><div class="k">峰峰值 ' + U.esc(r.u || '') + '</div></div></div>';
    },
    onTheme() { if (this.chart) this.chart.draw(); },
  };
  App.register('home', P);
})();
