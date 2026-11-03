// 记录: trend, filters, records grouped by day; record detail with waveform,
// spectrum, 3D time-frequency waterfall, quality, context and export.
(function () {
  const List = {
    render(el) {
      this.el = el;
      this.filter = 'all';
      this.q = '';
      el.innerHTML = App.header('记录', '<button class="icon-btn" id="rRef">' + Icon('refresh') + '</button>') +
        '<div class="card tight" data-tilt><h3>' + Icon('trend', 's') + '峰峰值趋势<span class="right" id="rCount"></span></h3><canvas class="chart short" id="rTrend"></canvas></div>' +
        '<div class="search">' + Icon('search') + '<input class="input" id="rQ" placeholder="搜索" autocomplete="off"></div>' +
        '<div class="seg" id="rSeg"><button data-f="all" class="on">全部</button><button data-f="normal">正常</button><button data-f="warn">预警</button><button data-f="alarm">报警</button><button data-f="bad">未通过</button></div>' +
        '<div id="rList" class="mt8"></div>';
      this.trend = new Charts.Chart(U.$('#rTrend', el), { padL: 36, xTicks: 4, yTicks: 3 });
      U.$('#rRef', el).onclick = async () => { const b = U.$('#rRef', el); b.innerHTML = Icon('refresh', 'spin'); await App.loadRecords(true); b.innerHTML = Icon('refresh'); };
      U.$('#rQ', el).oninput = (e) => { this.q = e.target.value.trim().toLowerCase(); this.draw(); };
      U.$('#rSeg', el).addEventListener('click', (e) => {
        const b = e.target.closest('[data-f]');
        if (!b) return;
        U.$$('#rSeg button', el).forEach((x) => x.classList.toggle('on', x === b));
        this.filter = b.getAttribute('data-f');
        U.haptic(6);
        this.draw();
      });
      U.$('#rList', el).addEventListener('click', (e) => { const r = e.target.closest('[data-id]'); if (r) App.push('record', { id: r.getAttribute('data-id') }); });
      this.onRecords(App.records);
    },
    reset() { this.onRecords(App.records); },
    show() { App.loadRecords(); },
    onRecords(recs) { this.recs = recs; this.draw(); },
    draw() {
      const el = this.el;
      if (!el) return;
      const box = U.$('#rList', el);
      if (!this.recs) { box.innerHTML = '<div class="list"><div class="rec"><div class="main"><div class="skeleton" style="height:40px"></div></div></div><div class="rec"><div class="main"><div class="skeleton" style="height:40px"></div></div></div></div>'; return; }
      const f = this.filter, q = this.q;
      const rows = this.recs.filter((r) => {
        if (f === 'bad' ? r.ok : f !== 'all' && (!r.ok || r.lv !== f)) return false;
        if (!q) return true;
        return [r.id, r.pt, r.pn, U.hz(r.hz)].join(' ').toLowerCase().indexOf(q) >= 0;
      });
      U.$('#rCount', el).textContent = this.recs.length + ' 条';
      // Trend: oldest to newest, points coloured by level.
      const tr = this.recs.filter((r) => r.ok && r.pp != null && r.u === 'mm').slice(0, 40).reverse();
      const warn = (App.live && App.live.set && App.live.set.warn) || 2, alarm = (App.live && App.live.set && App.live.set.alarm) || 5;
      if (tr.length >= 2) {
        this.trend.set({
          series: [{ x: tr.map((_, i) => i + 1), y: tr.map((r) => r.pp), dots: true, dotColors: tr.map((r) => Charts.css('--' + U.level(r.lv).c)) }],
          zeroBase: true, xInt: true, hlines: [{ y: warn, color: Charts.css('--warn'), label: '预警 ' + warn }, { y: alarm, color: Charts.css('--alarm'), label: '报警 ' + alarm }],
          yMin: 0, yMax: Math.max(alarm * 1.15, Math.max.apply(null, tr.map((r) => r.pp)) * 1.15),
        });
      } else this.trend.set({ series: [], empty: '至少两条毫米记录后显示趋势' });
      if (!rows.length) {
        box.innerHTML = '<div class="empty-state">' + Icon('list') + '<h4>' + (this.recs.length ? '没有符合条件的记录' : '还没有测量记录') + '</h4><div>' + (this.recs.length ? '换个筛选条件试试' : '完成一次测量后会出现在这里') + '</div></div>';
        return;
      }
      let html = '', day = null, open = false;
      rows.forEach((r) => {
        const k = U.dayKey(r.t);
        if (k !== day) {
          if (open) html += '</div>';
          html += '<div class="day">' + U.dayLabel(k) + '</div><div class="list stagger">';
          day = k; open = true;
        }
        html += App.pages.home.recRow(r);
      });
      if (open) html += '</div>';
      box.innerHTML = html;
    },
    onTheme() { if (this.trend) this.trend.draw(); },
  };
  App.register('records', List);

  // ------------------------------------------------------------------ detail
  const Detail = {
    render(el, params) {
      this.el = el;
      this.id = params.id;
      this.view = 'wave';
      el.innerHTML = App.pushHeader('测量详情', '<button class="icon-btn" id="dShare">' + Icon('share') + '</button>') +
        '<div id="dBody"><div class="skeleton" style="height:180px;margin-bottom:14px"></div><div class="skeleton" style="height:240px"></div></div>';
      U.$('#dShare', el).onclick = () => this.share();
      this.load();
    },
    destroy() { if (this.c1) this.c1.destroy(); if (this.wf) this.wf.stop(); },
    async load() {
      const r = await App.dev.record(this.id);
      if (!r || !r.ok) {
        const s = (App.records || []).find((x) => x.id === this.id);
        U.$('#dBody', this.el).innerHTML = '<div class="empty-state">' + Icon('file') + '<h4>' + U.errText(r && r.error) + '</h4><div>' +
          (s ? '主频 ' + U.hz(s.hz) + ' Hz · 峰峰值 ' + U.num(s.pp) + ' ' + s.u : '') + '</div></div>';
        return;
      }
      this.d = r.data;
      this.draw();
    },
    draw() {
      const d = this.d, rec = d.record || {}, res = rec.result || {}, sum = d.summary || {};
      const unit = res.unit || sum.u || 'px';
      const mm = unit === 'mm';
      const hz = res.dominant_frequency_hz != null ? res.dominant_frequency_hz : sum.hz;
      const pp = mm ? (res.displacement_peak_to_peak_mm != null ? res.displacement_peak_to_peak_mm : sum.pp) : (res.displacement_peak_to_peak_px != null ? res.displacement_peak_to_peak_px : sum.pp);
      const rms = mm ? (res.displacement_rms_mm != null ? res.displacement_rms_mm : sum.rms) : (res.displacement_rms_px != null ? res.displacement_rms_px : sum.rms);
      const lv = res.level || sum.lv || 'unknown';
      const valid = res.valid != null ? res.valid : sum.ok;
      const clear = res.peak_clear !== false && sum.clr !== false;
      this.unit = unit;
      const L = U.level(lv);
      const ptTxt = (rec.point_id || sum.pt || '') + (rec.point_name ? ' · ' + rec.point_name : '');
      const timeTxt = U.timeText(rec.start_local || sum.t);
      const q = rec.quality || {}, cam = rec.camera || {}, env = rec.environment || {}, st = rec.stereo || {}, ac = rec.acoustic || {};
      const kv = (k, v) => (v == null || v === '' ? '' : '<div class="k">' + k + '</div><div class="v">' + v + '</div>');
      const yes = (b) => (b ? '开' : '关');
      let html =
        '<div class="card fade-in" data-tilt style="background:linear-gradient(160deg,var(--' + (valid ? L.c : 'unknown') + '-soft),var(--surface) 60%)">' +
        '<div class="flex"><div class="grow"><div style="color:var(--text-2);font-size:13px">' + U.esc(ptTxt || '未指定测点') + '</div>' +
        '<div style="color:var(--text-3);font-size:12px">' + U.esc(timeTxt) + '</div></div>' + (valid ? U.levelPill(lv) : '<span class="pill warn">未通过质量检查</span>') + '</div>' +
        '<div class="flex mt16" style="align-items:flex-end"><div class="grow"><div class="muted" style="font-size:12px">主频</div><div class="num" style="font-size:44px;font-weight:700;line-height:1">' +
        (valid && clear ? U.hz(hz) : '无明确') + '<small style="font-size:15px;color:var(--text-2)"> Hz</small></div>' +
        (hz > 0 ? '<div class="muted num" style="font-size:13px;margin-top:6px">' + (hz * 60).toFixed(0) + ' rpm</div>' : '') + '</div>' +
        '<div style="text-align:right"><div class="muted" style="font-size:12px">峰峰值</div><div class="num" style="font-size:22px;font-weight:650">' + (pp != null ? U.num(pp) : '—') + ' <small style="font-size:12px;color:var(--text-2)">' + unit + '</small></div>' +
        '<div class="muted" style="font-size:12px;margin-top:4px">RMS ' + (rms != null ? U.num(rms) : '—') + ' ' + unit + '</div></div></div>' +
        (res.reason || sum.why ? '<div class="msg mt12">' + Icon('info', 's') + U.esc(res.reason || sum.why) + '</div>' : '') + '</div>' +
        '<div class="seg" id="dSeg"><button data-v="wave" class="on">时域波形</button><button data-v="spec">频谱</button><button data-v="3d">3D 时频</button></div>' +
        '<div class="card tight mt12"><canvas class="chart tall" id="dChart"></canvas><canvas class="chart" id="dWf" style="height:260px;display:none;touch-action:none"></canvas>' +
        '<div class="readout" id="dRead"></div></div>';
      const qk = [];
      if (res.peak_snr != null) qk.push(['谱峰信噪比', U.num(res.peak_snr, 1)]);
      if (q.min_ncc != null) qk.push(['最低相关度', U.num(q.min_ncc, 3)]);
      if (cam.frames_dropped != null) qk.push(['丢帧', cam.frames_dropped]);
      if (q.bad_frames != null) qk.push(['异常帧', q.bad_frames]);
      if (cam.fps_measured != null) qk.push(['实测帧率', U.num(cam.fps_measured, 1) + ' fps']);
      if (res.tracker) qk.push(['跟踪算法', String(res.tracker).toUpperCase()]);
      if (qk.length) html += '<div class="section-title">质量指标</div><div class="grid3">' + qk.map((x) => '<div class="stat"><div class="k">' + x[0] + '</div><div class="v num" style="font-size:17px">' + x[1] + '</div></div>').join('') + '</div>';
      if (rec.measurement_id) {
        html += '<div class="section-title">采集条件</div><div class="card"><div class="kv">' +
          kv('记录编号', U.esc(rec.measurement_id)) + kv('设备', U.esc(rec.device_id)) + kv('测量方式', res.method === 'multipoint' ? '多点（停机基线对比）' : '单框跟踪') +
          kv('相机', cam.width ? cam.width + '×' + cam.height + ' · ' + (cam.frames || '') + ' 帧' : null) +
          kv('测量框', rec.roi ? rec.roi.w + '×' + rec.roi.h + ' px @ (' + rec.roi.x + ', ' + rec.roi.y + ')' : null) +
          kv('标定', rec.calibration && rec.calibration.mm_per_pixel ? U.num(rec.calibration.mm_per_pixel, 5) + ' mm/px · ' + (rec.calibration.axis || '').toUpperCase() + ' 轴' : '未标定（像素）') +
          kv('照明', rec.lighting ? yes(rec.lighting.on) + (rec.lighting.on ? ' · ' + rec.lighting.percent + '%' : '') : null) +
          kv('十字投影', rec.laser_projector ? (rec.laser_projector.switched_off_for_capture ? '采集时自动关闭' : yes(rec.laser_projector.on)) : null) +
          kv('温度 / 湿度', env.valid ? U.num(env.temperature_c, 1) + ' °C · ' + U.num(env.humidity_percent, 0) + ' %' : null) +
          kv('测距', env.distance_mm ? (env.distance_mm / 1000).toFixed(2) + ' m' : null) +
          kv('双目深度', st.enabled ? (st.roi_depth_valid ? (st.roi_depth_mm / 1000).toFixed(2) + ' m · ' + (st.backend || '') : '无有效深度') : null) +
          kv('声学参考', ac.enabled ? (ac.valid ? '一致 · ' + U.hz(ac.analysis && ac.analysis.frequency_hz) + ' Hz' : '不可用') : null) +
          kv('时间来源', rec.time_valid ? (rec.time_source || '').toUpperCase() : '未同步') +
          '</div></div>';
      }
      html += '<div class="grid2 mt16"><button class="btn" id="dPrint">' + Icon('print', 's') + '打印报告</button>' +
        '<button class="btn" id="dExport"' + (App.dev.caps.files ? '' : ' disabled') + '>' + Icon('download', 's') + '导出数据</button></div>';
      U.$('#dBody', this.el).innerHTML = html;
      this.c1 = new Charts.Chart(U.$('#dChart', this.el), { interactive: true, padL: 46, onReadout: (r) => this.readout(r) });
      this.wf = new FX3D.Waterfall(U.$('#dWf', this.el));
      U.$('#dSeg', this.el).addEventListener('click', (e) => {
        const b = e.target.closest('[data-v]');
        if (!b) return;
        U.$$('#dSeg button', this.el).forEach((x) => x.classList.toggle('on', x === b));
        this.view = b.getAttribute('data-v');
        U.haptic(6);
        this.plot();
      });
      U.$('#dPrint', this.el).onclick = async () => { if (await U.confirm('打印测量报告', '设备热敏打印机将打印本条记录。', '打印')) App.cmd('print', { measurement_id: this.id }, '已发送到打印机'); };
      U.$('#dExport', this.el).onclick = () => this.export();
      this.peak = { hz, clear };
      this.plot();
    },
    plot() {
      const d = this.d;
      const wfC = U.$('#dWf', this.el), c = U.$('#dChart', this.el);
      U.$('#dRead', this.el).textContent = '';
      if (this.view === '3d') {
        c.style.display = 'none'; wfC.style.display = '';
        const w = d.waveform || {};
        const fMax = this.peak.hz > 0 ? Math.min(this.peak.hz * 3.2, 1e9) : null;
        this.wf.set(w.t, w.v, fMax);
        return;
      }
      wfC.style.display = 'none'; c.style.display = '';
      if (this.view === 'wave') {
        const w = d.waveform || {};
        this.c1.set({ series: [{ x: w.t || [], y: w.v || [], width: 1.4 }], symmetric: true, xUnit: 's', empty: '无波形数据' });
      } else {
        const s = d.spectrum || {};
        const f = s.f || [], a = s.a || [];
        const markers = [];
        if (f.length) {
          let bi = 0;
          for (let i = 1; i < a.length; i++) if (a[i] > a[bi]) bi = i;
          markers.push({ x: f[bi], y: a[bi], label: (this.peak.clear ? '' : '最大峰 ') + U.hz(f[bi]) + ' Hz', color: Charts.css('--accent') });
        }
        this.c1.set({ series: [{ x: f, y: a, width: 1.6 }], zeroBase: true, xUnit: '', markers, empty: '无频谱数据' });
      }
    },
    readout(r) {
      const el = U.$('#dRead', this.el);
      if (!r) { el.textContent = ''; return; }
      el.textContent = this.view === 'wave' ? 't = ' + r.x.toFixed(3) + ' s · ' + U.num(r.y) + ' ' + this.unit : U.hz(r.x) + ' Hz · 幅值 ' + U.num(r.y) + ' ' + this.unit;
    },
    summaryText() {
      const d = this.d || {}, rec = d.record || {}, res = rec.result || {}, sum = d.summary || {};
      const u = res.unit || sum.u || '';
      return '见微测振 · 测量摘要\n测点：' + (rec.point_id || sum.pt || '—') + ' ' + (rec.point_name || '') + '\n时间：' + U.timeText(rec.start_local || sum.t) +
        '\n主频：' + U.hz(res.dominant_frequency_hz || sum.hz) + ' Hz\n峰峰值：' + U.num(u === 'mm' ? res.displacement_peak_to_peak_mm : res.displacement_peak_to_peak_px) + ' ' + u +
        '\n判级：' + U.level(res.level || sum.lv).t + '\n编号：' + this.id;
    },
    share() {
      const text = this.summaryText();
      try {
        if (window.VibmonNative && VibmonNative.shareText) { VibmonNative.shareText('测量摘要', text); return; }
        if (navigator.share) { navigator.share({ title: '测量摘要', text }); return; }
        if (navigator.clipboard) { navigator.clipboard.writeText(text); U.toast('摘要已复制'); }
      } catch (e) { U.toast('无法分享', 'err'); }
    },
    async export() {
      const files = ['record.json', 'waveform.csv', 'spectrum.csv'];
      U.toast('正在导出…');
      let n = 0;
      for (const f of files) {
        const txt = await App.dev.fetchText(App.dev.fileUrl(this.id, f));
        if (txt == null) continue;
        const name = this.id + '_' + f;
        const mime = f.endsWith('.json') ? 'application/json' : 'text/csv';
        if (window.VibmonNative && VibmonNative.saveFile) { if (VibmonNative.saveFile(name, mime, txt)) n++; }
        else {
          const a = document.createElement('a');
          a.href = URL.createObjectURL(new Blob([txt], { type: mime }));
          a.download = name; a.click(); n++;
        }
      }
      U.toast(n ? '已导出 ' + n + ' 个文件到「下载」' : '导出失败', n ? 'ok' : 'err');
    },
  };
  App.register('record', Detail);
})();
