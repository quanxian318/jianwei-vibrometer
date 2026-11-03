// 测量: camera preview with the measuring box, start/stop with progress,
// duration, lighting/projector, live displacement and the last result.
(function () {
  const RC = 2 * Math.PI * 66;
  const P = {
    render(el) {
      this.el = el;
      el.innerHTML = App.header('测量', '<button class="icon-btn" id="mSet">' + Icon('sliders') + '</button>') +
        '<div class="card" style="padding:12px" id="mPrevCard">' +
        '<div class="preview" id="mPrev"><img id="mImg" alt=""><i class="vf"></i><div class="roi" id="mRoi" style="display:none"></div>' +
        '<div class="tag" id="mTag"></div><div class="empty" id="mEmpty">' + Icon('camera', 'l') + '<span>正在获取画面…</span></div></div>' +
        '</div>' +
        '<div class="card"><div class="go-wrap">' +
        '<button class="go" id="mGo"><svg viewBox="0 0 148 148"><defs><linearGradient id="gradRing" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="var(--accent)"/><stop offset="1" stop-color="var(--accent-2)"/></linearGradient></defs>' +
        '<circle class="ring-bg" cx="74" cy="74" r="66" fill="none" stroke-width="6"/>' +
        '<circle class="ring-fg" id="mRing" cx="74" cy="74" r="66" fill="none" stroke-width="6" stroke-linecap="round" stroke-dasharray="' + RC + '" stroke-dashoffset="' + RC + '"/></svg>' +
        '<i class="go-ring"></i><div class="core" id="mCore">' + Icon('play', 'l') + '<span>开始测量</span></div></button>' +
        '<div class="go-sub" id="mSub"></div></div>' +
        '<div class="section-title" style="margin-top:14px">采集时长</div><div class="chips" id="mDur"></div>' +
        '<div id="mBase"></div></div>' +
        '<div class="section-title">实时位移</div>' +
        '<div class="card tight"><canvas class="chart tall" id="mChart"></canvas><div class="readout" id="mRead"></div></div>' +
        '<div class="section-title">照明与投影</div>' +
        '<div class="list">' +
        '<div class="row static"><div class="ic accent">' + Icon('light') + '</div><div class="main"><div class="title">照明灯</div><div class="desc num" id="mLightD"></div></div><label class="switch"><input type="checkbox" id="mLight"><i></i></label></div>' +
        '<div class="row static" style="flex-wrap:wrap"><div class="main" style="flex-basis:100%;padding-left:48px"><input type="range" min="5" max="100" step="5" id="mPct"></div></div>' +
        '<div class="row static"><div class="ic" style="background:var(--alarm-soft);color:var(--alarm)">' + Icon('laser') + '</div><div class="main"><div class="title">十字投影</div></div><label class="switch"><input type="checkbox" id="mLaser"><i></i></label></div>' +
        '</div>' +
        '<div id="mResult"></div>';
      this.chart = new Charts.Chart(U.$('#mChart', el), { interactive: true, padL: 44, onReadout: (r) => {
        U.$('#mRead', el).textContent = r ? 't = ' + r.x.toFixed(2) + ' s · ' + U.num(r.y) + ' ' + this.unit : '';
      } });
      U.$('#mGo', el).onclick = () => this.toggle();
      U.$('#mSet', el).onclick = () => App.push('settings', { section: 'measure' });
      U.$('#mDur', el).addEventListener('click', (e) => {
        const c = e.target.closest('[data-d]');
        if (!c) return;
        const d = +c.getAttribute('data-d');
        this.duration = d;
        U.store.set('duration', d);
        this.renderDur();
        U.haptic(6);
        App.dev && App.dev.command('set', { duration_s: d }).then((r) => { if (r && r.ok) { App.settings = r.data || App.settings; } });
      });
      U.$('#mLight', el).onchange = (e) => App.cmd('light', { on: e.target.checked });
      U.$('#mLaser', el).onchange = (e) => App.cmd('laser', { on: e.target.checked });
      const pct = U.$('#mPct', el);
      pct.oninput = () => { pct.style.setProperty('--p', ((pct.value - 5) / 95 * 100) + '%'); U.$('#mLightD', el).textContent = pct.value + '%'; };
      pct.onchange = () => App.cmd('light', { percent: +pct.value });
      U.$('#mPrev', el).addEventListener('click', (e) => this.tapPreview(e));
      U.$('#mBase', el).addEventListener('click', (e) => { if (e.target.closest('[data-base]')) this.baseline(); });
      U.$('#mResult', el).addEventListener('click', (e) => { const r = e.target.closest('[data-id]'); if (r) App.push('record', { id: r.getAttribute('data-id') }); });
      this.duration = U.store.get('duration', 10);
      this.renderDur();
      App.on((l) => this.update(l));
      this.update(App.live);
    },
    reset() { this.update(App.live); },
    onSettings() { if (App.settings && App.settings.duration_s) { this.duration = App.settings.duration_s; this.renderDur(); } },
    renderDur() {
      const opts = [5, 10, 20, 30, 60];
      if (opts.indexOf(this.duration) < 0) opts.push(this.duration);
      U.$('#mDur', this.el).innerHTML = opts.sort((a, b) => a - b).map((d) => '<button class="chip' + (d === this.duration ? ' on' : '') + '" data-d="' + d + '">' + d + ' s</button>').join('');
    },
    show() { this.visible = true; this.refreshPreview(); },
    hide() { this.visible = false; clearTimeout(this.prevTimer); },
    refreshPreview() {
      clearTimeout(this.prevTimer);
      if (!this.visible || !App.dev) return;
      const img = U.$('#mImg', this.el);
      const card = U.$('#mPrevCard', this.el);
      if (!App.dev.caps.preview) {
        card.style.display = 'none';
        return;
      }
      card.style.display = '';
      const url = App.dev.previewUrl(480);
      const next = new Image();
      next.onload = () => {
        img.src = next.src;
        U.$('#mEmpty', this.el).style.display = 'none';
        this.prevTimer = setTimeout(() => this.refreshPreview(), App.live && App.live.ms === 'capturing' ? 1500 : 700);
      };
      next.onerror = () => {
        U.$('#mEmpty', this.el).style.display = '';
        U.$('#mEmpty', this.el).innerHTML = Icon('camera', 'l') + '<span>暂无画面</span>';
        this.prevTimer = setTimeout(() => this.refreshPreview(), 2500);
      };
      next.src = url;
    },
    tapPreview(e) {
      const l = App.live;
      if (!l || !App.dev.caps.roi || l.ms === 'capturing' || l.ms === 'analyzing') return;
      const img = U.$('#mImg', this.el);
      const r = img.getBoundingClientRect();
      const box = this.imgBox(r, l);
      const x = (e.clientX - box.x) / box.w, y = (e.clientY - box.y) / box.h;
      if (x < 0 || x > 1 || y < 0 || y > 1) return;
      l.roi = [x, y, l.roi ? l.roi[2] : 64, l.roi ? l.roi[3] : 64];
      this.drawRoi(l);
      App.cmd('roi', { x: +x.toFixed(4), y: +y.toFixed(4) }, '测量框已移动');
    },
    // Displayed image rectangle inside the contain-fitted <img>.
    imgBox(r, l) {
      const fw = l.fw || 4, fh = l.fh || 3;
      const s = Math.min(r.width / fw, r.height / fh);
      const w = fw * s, h = fh * s;
      return { x: r.left + (r.width - w) / 2, y: r.top + (r.height - h) / 2, w, h, ox: (r.width - w) / 2, oy: (r.height - h) / 2, s };
    },
    drawRoi(l) {
      const roi = U.$('#mRoi', this.el);
      if (!l.roi || !l.fw) { roi.style.display = 'none'; return; }
      const img = U.$('#mImg', this.el);
      const r = img.getBoundingClientRect();
      if (!r.width) return;
      const b = this.imgBox(r, l);
      const w = l.roi[2] * b.s, h = l.roi[3] * b.s;
      roi.style.display = '';
      roi.style.left = (b.ox + l.roi[0] * b.w - w / 2) + 'px';
      roi.style.top = (b.oy + l.roi[1] * b.h - h / 2) + 'px';
      roi.style.width = w + 'px'; roi.style.height = h + 'px';
    },
    async toggle() {
      const l = App.live || {};
      if (l.ms === 'capturing') {
        const ok = await U.confirm('提前结束采集？', '将用已采集的数据进行分析。', '结束并分析');
        if (ok) App.cmd('measure_stop');
        return;
      }
      if (l.ms === 'analyzing') return;
      this.start();
    },
    async start() {
      const l = App.live || {};
      if (l.method === 'multipoint' && l.bl === false) {
        const go = await U.confirm('需要停机基线', '多点测振需先在电机停机时采集基线（约 15 秒），再开机测量。', '现在采集基线');
        if (go) this.baseline();
        return;
      }
      U.haptic(18);
      const r = await App.cmd('measure_start', { duration_s: this.duration });
      if (r.ok) U.toast('开始采集 ' + this.duration + ' 秒');
    },
    async baseline() {
      const ok = await U.confirm('采集停机基线', '请确认电机已停机、机位与测量框不再移动。', '开始采集');
      if (ok) App.cmd('baseline_start', {}, '正在采集基线');
    },
    update(l) {
      const el = this.el;
      if (!el || !l) return;
      const capturing = l.ms === 'capturing', analyzing = l.ms === 'analyzing';
      const go = U.$('#mGo', el), core = U.$('#mCore', el), ring = U.$('#mRing', el);
      go.classList.toggle('running', capturing || analyzing);
      if (capturing) {
        const f = U.clamp(l.el / Math.max(0.1, l.dur), 0, 1);
        ring.style.strokeDashoffset = RC * (1 - f);
        core.innerHTML = '<span class="t num">' + Math.max(0, l.dur - l.el).toFixed(1) + '</span><small>秒</small>';
        U.$('#mSub', el).textContent = '正在采集';
      } else if (analyzing) {
        ring.style.strokeDashoffset = 0;
        core.innerHTML = Icon('refresh', 'l spin') + '<small>分析中</small>';
        U.$('#mSub', el).textContent = '';
      } else {
        ring.style.strokeDashoffset = RC;
        core.innerHTML = Icon('play', 'l') + '<span>开始测量</span>';
        U.$('#mSub', el).textContent = !l.cam ? '相机未就绪' : (l.pt && l.pt.id ? l.pt.id + (l.pt.name ? ' · ' + l.pt.name : '') : '');
      }
      U.$('#mTag', el).innerHTML = capturing ? '<span class="pill alarm"><span class="led live"></span>REC</span>' : '<span class="pill">' + (U.STATE[l.ms] || '') + '</span>';
      if (l.fw && l.fh) U.$('#mPrev', el).style.aspectRatio = l.fw + ' / ' + l.fh;
      this.drawRoi(l);
      // Multi-point: baseline state.
      const base = U.$('#mBase', el);
      const bh = l.method === 'multipoint' ? '<div class="banner ' + (l.bl ? 'info' : 'warn') + ' mt16 mb0">' + Icon('grid') + '<span class="grow">多点测振 · ' +
        (l.bl ? '停机基线已就绪' : '尚无停机基线') + '</span><button class="btn" data-base>' + (l.bl ? '重新采集' : '采集基线') + '</button></div>' : '';
      if (base.innerHTML !== bh) base.innerHTML = bh;
      U.$('#mLight', el).checked = !!(l.light && l.light.on);
      U.$('#mLaser', el).checked = !!l.laser;
      const pct = U.$('#mPct', el);
      if (document.activeElement !== pct && l.light) { pct.value = l.light.pct; pct.oninput(); }
      // Live chart (seconds on x).
      const unit = l.mmpx ? 'mm' : 'px';
      this.unit = unit;
      if (l.wave && l.wave.length) {
        const k = l.mmpx || 1, fps = l.fps || 45;
        const y = l.wave.map((v) => v * k);
        const mean = y.reduce((a, b) => a + b, 0) / y.length;
        this.chart.set({ series: [{ x: y.map((_, i) => (i - y.length + 1) / fps), y: y.map((v) => v - mean) }], symmetric: true, xUnit: 's' });
      } else {
        this.chart.set({ series: [], empty: '暂无实时数据' });
      }
      // Last result card.
      const res = U.$('#mResult', el);
      const last = l.last;
      const html = last && !capturing && !analyzing ? '<div class="section-title">上次结果</div><div class="card" data-id="' + U.esc(last.id) + '" data-tilt style="cursor:pointer">' +
        '<div class="flex"><div class="grow"><div style="font-size:13px;color:var(--text-2)">主频</div><div class="num" style="font-size:34px;font-weight:700">' + (last.clr === false ? '无明确' : U.hz(last.hz)) + '<small style="font-size:14px;color:var(--text-2)"> Hz</small></div></div>' +
        (last.ok ? U.levelPill(last.lv) : '<span class="pill warn">未通过</span>') + '</div>' +
        '<div class="grid3 mt12"><div><div class="muted" style="font-size:12px">峰峰值</div><div class="num" style="font-weight:650;font-size:17px">' + (last.amp === false ? '—' : U.num(last.pp)) + ' ' + last.u + '</div></div>' +
        '<div><div class="muted" style="font-size:12px">RMS</div><div class="num" style="font-weight:650;font-size:17px">' + (last.amp === false ? '—' : U.num(last.rms)) + ' ' + last.u + '</div></div>' +
        '<div style="text-align:right;align-self:end;color:var(--accent);font-size:14px">详情 ›</div></div>' +
        '</div>' : '';
      if (res.innerHTML !== html) res.innerHTML = html;
    },
    onTheme() { if (this.chart) this.chart.draw(); },
  };
  App.register('measure', P);
})();
