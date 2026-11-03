// Demo device: the same API surface as Api.Device with synthetic data, so the
// app can be shown without hardware. Values are illustrative only.
(function () {
  const rnd = (a, b) => a + Math.random() * (b - a);
  const pad = (n) => String(n).padStart(2, '0');
  const localText = (d) => d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + ' ' + pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds());
  const POINTS = [
    { uid: '04A1B2C3D4E5F6', id: 'P-01', name: '1# 循环泵 驱动端' },
    { uid: '04A1B2C3D4E5F7', id: 'P-02', name: '1# 循环泵 非驱动端' },
    { uid: '04A1B2C3D4E5F8', id: 'P-03', name: '2# 引风机 轴承座' },
    { uid: '04A1B2C3D4E5F9', id: 'P-04', name: '空压机 底座' },
  ];

  function makeRecord(i, when, point, hz, pp, mmPerPx) {
    const fps = 45;
    const n = 450;
    const t = [], v = [];
    for (let k = 0; k < n; k++) {
      const tt = k / fps;
      t.push(+tt.toFixed(4));
      v.push(+(pp / 2 * Math.sin(2 * Math.PI * hz * tt) + pp * 0.18 * Math.sin(2 * Math.PI * hz * 2 * tt + 1) + rnd(-1, 1) * pp * 0.05).toFixed(5));
    }
    const f = [], a = [];
    for (let k = 1; k <= 225; k++) {
      const ff = k * fps / n;
      let amp = pp * 0.004 * rnd(0.5, 1.5);
      amp += pp / 2 * Math.exp(-Math.pow((ff - hz) / 0.12, 2)) + pp * 0.09 * Math.exp(-Math.pow((ff - 2 * hz) / 0.12, 2));
      f.push(+ff.toFixed(4)); a.push(+amp.toFixed(6));
    }
    const lv = pp >= 5 ? 'alarm' : pp >= 2 ? 'warn' : 'normal';
    const rms = pp / 2 / Math.SQRT2 * 1.02;
    const id = 'vibmon-demo-' + (1000 + i);
    return {
      summary: { id, t: localText(when), pt: point.id, pn: point.name, ok: true, u: 'mm', hz: +hz.toFixed(3), clr: true, pp: +pp.toFixed(4), rms: +rms.toFixed(4), lv },
      detail: {
        id,
        record: {
          measurement_id: id, device_id: 'vibmon-demo', point_id: point.id, point_name: point.name, nfc_uid_hex: point.uid,
          start_local: localText(when), time_valid: true, time_source: 'ntp',
          camera: { source: 'pl_vdma', width: 1280, height: 720, fps_measured: 45.0, frames: n, frames_dropped: 0 },
          roi: { x: 608, y: 328, w: 64, h: 64 },
          calibration: { id: 'cal-demo', axis: 'y', mm_per_pixel: mmPerPx },
          result: {
            valid: true, unit: 'mm', method: 'roi', tracker: 'ncc', window: 'hann', level: lv, peak_clear: true, peak_snr: +rnd(40, 400).toFixed(1),
            dominant_frequency_hz: hz, displacement_peak_to_peak_mm: pp, displacement_rms_mm: rms,
            thresholds: { unit: 'mm', warn_peak_to_peak: 2, alarm_peak_to_peak: 5 },
            field_spectrum: { valid: true, peak_hz: hz * rnd(0.99, 1.01) },
          },
          quality: { bad_frames: 0, search_edge_hits: 0, min_ncc: +rnd(0.93, 0.99).toFixed(3) },
          lighting: { on: true, percent: 80 }, laser_projector: { on: false, switched_off_for_capture: true },
          environment: { valid: true, temperature_c: +rnd(22, 31).toFixed(1), humidity_percent: +rnd(35, 60).toFixed(0), distance_mm: Math.round(rnd(600, 1400)) },
          stereo: { enabled: true, backend: 'PL', roi_depth_valid: true, roi_depth_mm: Math.round(rnd(600, 1400)) },
          acoustic: { enabled: true, available: true, valid: true, analysis: { relation: 'agree', frequency_hz: hz } },
          versions: { app: '0.2.0', nano: '0.2.0', hardware: '2026-10-20' },
        },
        waveform: { t, v, samples: n },
        spectrum: { f, a, bins: f.length },
      },
    };
  }

  class DemoDevice {
    constructor() {
      this.entry = { kind: 'demo', host: 'demo', port: 0, name: '演示设备 · 见微 V1', id: 'vibmon-demo' };
      this.kind = 'demo';
      this.lan = true;
      this.caps = { preview: true, files: false, fullRecords: true, roi: true, fullStatus: true, nanoNet: false, gatewayNet: true, shutdown: true };
      this.t0 = Date.now();
      this.state = { ms: 'preview', el: 0, dur: 10, light: true, pct: 80, laser: false, mute: false, pt: POINTS[0], roi: [0.42, 0.5, 96, 96], startedAt: 0 };
      this.settingsV = { duration_s: 10, roi_width_px: 64, roi_height_px: 64, axis: 'y', auto_light: true, light_percent: 80, laser_off_during_measurement: true, warn_pp: 2, alarm_pp: 5, buzzer: true, voice: true, volume: 40, fan_mode: 'auto', led_brightness: 32, vision_low_hz: 2, vision_high_hz: 5, vision_gain: 20 };
      this.recs = [];
      const now = Date.now();
      const plan = [[0, 12.48, 0.62], [1, 12.51, 0.71], [2, 24.9, 2.42], [3, 49.6, 0.35], [0, 12.46, 0.93], [1, 12.5, 1.12], [2, 24.88, 3.1], [0, 12.47, 1.35], [3, 49.7, 0.41], [2, 24.92, 5.6], [0, 12.49, 1.62], [1, 12.52, 1.21]];
      plan.forEach((p, i) => {
        const when = new Date(now - (plan.length - i) * 7.3 * 3600 * 1000);
        this.recs.unshift(makeRecord(i, when, POINTS[p[0]], p[1], p[2], 0.0123));
      });
      this.hz = 12.5;
      this.amp = 1.2;
      this.netV = { station: { state: 'CONNECTED', ssid: 'Factory-5G', ip: '192.168.31.86', rssi: -54, ap: { enabled: true, ssid: 'VIBMON-3F2A', ip: '192.168.4.1', clients: 1 } }, mdns: 'vibmon-3f2a.local' };
      this.canvas = null;
    }
    elapsed() { return this.state.ms === 'capturing' ? (Date.now() - this.state.startedAt) / 1000 : 0; }
    async info() { return { ok: true, data: { service: 'vibmon', kind: 'demo', device_id: 'vibmon-demo', name: this.entry.name, version: 'vibmon 0.2.0' } }; }
    async live() {
      await U.sleep(60);
      const s = this.state;
      if (s.ms === 'capturing' && this.elapsed() >= s.dur) {
        s.ms = 'analyzing';
        setTimeout(() => this.finish(), 900);
      }
      const now = Date.now();
      const wave = [];
      for (let k = 0; k < 220; k++) {
        const tt = now / 1000 - (220 - k) / 45;
        wave.push(+(this.amp / 0.0123 / 2 * Math.sin(2 * Math.PI * this.hz * tt) * (0.9 + 0.1 * Math.sin(tt)) + rnd(-2, 2)).toFixed(3));
      }
      const last = this.recs[0] ? this.recs[0].summary : null;
      return {
        ok: true,
        data: {
          dev: 'vibmon-demo', ver: 'vibmon 0.2.0', ms: s.ms, el: +this.elapsed().toFixed(2), dur: s.dur, cam: true, method: 'roi', cal: true, unit: 'mm',
          lv: last ? last.lv : 'unknown', mute: s.mute, light: { on: s.light, pct: s.pct, ok: true }, laser: s.laser,
          pt: { id: s.pt.id, name: s.pt.name, uid: s.pt.uid }, pwr: { v: 12.08, a: +rnd(1.1, 1.4).toFixed(2), w: +rnd(13, 17).toFixed(1), warn: false, pf: false },
          tc: 27.4, rh: 46, dist: 862, tach: null, fan: false, prn: 'idle', msg: s.msg || '', tv: true, utc: now,
          nano: { on: true, wifi: 'CONNECTED', ip: '192.168.31.86', sd: true, nfc: true }, st: { on: true, be: 'PL', vf: 0.87 }, ac: 'ready',
          set: { dur: this.settingsV.duration_s, warn: this.settingsV.warn_pp, alarm: this.settingsV.alarm_pp, axis: 'y' }, roi: s.roi,
          last: last ? { id: last.id, ok: true, why: '', u: 'mm', hz: last.hz, clr: true, amp: true, pp: last.pp, rms: last.rms, lv: last.lv, saved: true } : undefined,
          fps: 45.0, fw: 1280, fh: 720, wave, mmpx: 0.0123,
        },
      };
    }
    finish() {
      const s = this.state;
      const pp = Math.max(0.2, this.amp * rnd(0.85, 1.25));
      const rec = makeRecord(this.recs.length + 20, new Date(), s.pt, this.hz * rnd(0.995, 1.005), pp, 0.0123);
      this.recs.unshift(rec);
      s.ms = 'preview';
      s.msg = '主频 ' + rec.summary.hz.toFixed(2) + ' Hz, 峰峰值 ' + rec.summary.pp.toFixed(3) + ' mm, ' + U.level(rec.summary.lv).t;
      if (this.restoreLaser) { s.laser = true; this.restoreLaser = false; }
    }
    async command(cmd, a) {
      await U.sleep(140);
      const s = this.state;
      a = a || {};
      switch (cmd) {
        case 'measure_start':
          if (s.ms === 'capturing' || s.ms === 'analyzing') return { ok: false, error: 'already_measuring' };
          s.dur = a.duration_s || this.settingsV.duration_s;
          s.ms = 'capturing'; s.startedAt = Date.now(); s.msg = '';
          if (s.laser) { s.laser = false; this.restoreLaser = true; }
          this.amp = [0.6, 0.9, 1.3, 1.8, 2.6][Math.floor(rnd(0, 5))];
          this.hz = [12.5, 24.9, 49.6][Math.floor(rnd(0, 3))];
          return { ok: true };
        case 'baseline_start': return { ok: false, error: 'baseline_not_used' };
        case 'measure_stop': if (s.ms === 'capturing') { s.ms = 'analyzing'; setTimeout(() => this.finish(), 600); } return { ok: true };
        case 'light': if (a.percent != null) s.pct = a.percent; if (a.on != null) s.light = a.on; return { ok: true };
        case 'laser':
          if (a.on && s.ms === 'capturing') return { ok: false, error: 'measuring' };
          s.laser = a.on != null ? a.on : !s.laser; return { ok: true };
        case 'mute': s.mute = a.on !== false; return { ok: true };
        case 'set_point': s.pt = POINTS.find((p) => p.id === a.id) || { id: a.id, name: a.name || '', uid: '' }; return { ok: true };
        case 'roi': s.roi = [a.x != null ? a.x : s.roi[0], a.y != null ? a.y : s.roi[1], a.w || s.roi[2], a.h || s.roi[3]]; return { ok: true };
        case 'set': Object.keys(a).forEach((k) => { if (k in this.settingsV) this.settingsV[k] = a[k]; }); return { ok: true, data: this.settingsV };
        case 'print': return { ok: true };
        case 'rtc_set': return { ok: true };
        case 'voice': return { ok: true };
        case 'set_net': return { ok: true };
        case 'shutdown': return { ok: true };
        case 'zoom_reset': return { ok: true };
        default: return { ok: false, error: 'unknown_command' };
      }
    }
    async records() { await U.sleep(120); return { ok: true, data: this.recs.map((r) => r.summary) }; }
    async record(id) { await U.sleep(160); const r = this.recs.find((x) => x.summary.id === id); return r ? { ok: true, data: r.detail } : { ok: false, error: 'no_record' }; }
    async settings() { return { ok: true, data: Object.assign({}, this.settingsV) }; }
    async points() { return { ok: true, data: POINTS }; }
    async status() { return { ok: true, data: {} }; }
    async net() { return { ok: true, data: this.netV }; }
    async setNet() { await U.sleep(300); return { ok: true, data: { accepted: true } }; }
    async scan() {
      await U.sleep(700);
      return { ok: true, data: { scanning: false, networks: [{ ssid: 'Factory-5G', rssi: -54, secure: true }, { ssid: 'Workshop-2', rssi: -63, secure: true }, { ssid: 'Guest', rssi: -78, secure: false }] } };
    }
    // A rendered "camera" frame: a motor-pump set on its base, in a workshop.
    previewUrl() {
      if (!this.canvas) this.canvas = document.createElement('canvas');
      const c = this.canvas, W = 640, H = 360;
      c.width = W; c.height = H;
      const g = c.getContext('2d');
      const t = Date.now() / 1000;
      // Workshop: wall, floor, warm out-of-focus lights.
      let gr = g.createLinearGradient(0, 0, 0, H);
      gr.addColorStop(0, '#1d232c'); gr.addColorStop(0.62, '#2a3038'); gr.addColorStop(0.63, '#20252c'); gr.addColorStop(1, '#111418');
      g.fillStyle = gr; g.fillRect(0, 0, W, H);
      [[90, 60, 50, '255,190,120'], [520, 40, 70, '255,210,150'], [330, 30, 36, '170,200,255'], [610, 150, 40, '255,180,110']].forEach((b) => {
        const r = g.createRadialGradient(b[0], b[1], 0, b[0], b[1], b[2]);
        r.addColorStop(0, 'rgba(' + b[3] + ',.35)'); r.addColorStop(1, 'rgba(' + b[3] + ',0)');
        g.fillStyle = r; g.fillRect(0, 0, W, H);
      });
      // Base frame.
      gr = g.createLinearGradient(0, 262, 0, 300);
      gr.addColorStop(0, '#59636f'); gr.addColorStop(1, '#2b3038');
      g.fillStyle = gr; rr(g, 70, 262, 500, 34, 6); g.fill();
      g.fillStyle = 'rgba(0,0,0,.35)'; g.fillRect(70, 296, 500, 10);
      [100, 230, 410, 540].forEach((x) => { g.fillStyle = '#9aa4b0'; g.beginPath(); g.arc(x, 279, 6, 0, 6.3); g.fill(); g.fillStyle = '#3a414b'; g.beginPath(); g.arc(x, 279, 2.5, 0, 6.3); g.fill(); });
      // The motor shakes a little at the running speed.
      const dy = Math.sin(t * 2 * Math.PI * 1.3) * 1.2;
      g.save(); g.translate(0, dy);
      // Feet.
      g.fillStyle = '#2f353d'; g.fillRect(140, 238, 40, 26); g.fillRect(300, 238, 40, 26);
      // Motor body (horizontal cylinder) with cooling fins.
      gr = g.createLinearGradient(0, 120, 0, 250);
      gr.addColorStop(0, '#6f8db3'); gr.addColorStop(0.28, '#a9c3e4'); gr.addColorStop(0.55, '#4d6b92'); gr.addColorStop(1, '#1e2c40');
      g.fillStyle = gr; rr(g, 110, 120, 250, 130, 18); g.fill();
      g.globalAlpha = 0.55;
      for (let x = 128; x < 350; x += 11) { g.fillStyle = 'rgba(10,20,35,.55)'; g.fillRect(x, 124, 3, 122); g.fillStyle = 'rgba(220,235,255,.25)'; g.fillRect(x + 3, 124, 1.5, 122); }
      g.globalAlpha = 1;
      // End shield and fan cowl.
      gr = g.createLinearGradient(0, 126, 0, 244);
      gr.addColorStop(0, '#8a9bb0'); gr.addColorStop(0.4, '#c7d2df'); gr.addColorStop(1, '#3b4657');
      g.fillStyle = gr; rr(g, 86, 128, 34, 114, 12); g.fill();
      // Terminal box.
      gr = g.createLinearGradient(0, 92, 0, 122);
      gr.addColorStop(0, '#8fa8c8'); gr.addColorStop(1, '#4a6283');
      g.fillStyle = gr; rr(g, 196, 94, 70, 30, 5); g.fill();
      g.fillStyle = 'rgba(255,255,255,.6)'; g.font = '600 9px Inter, sans-serif'; g.fillText('380V 7.5kW', 204, 112);
      // Nameplate.
      g.fillStyle = 'rgba(230,236,244,.85)'; rr(g, 236, 168, 46, 26, 3); g.fill();
      g.fillStyle = 'rgba(40,50,65,.8)'; for (let i = 0; i < 4; i++) g.fillRect(241, 173 + i * 5, 34 - i * 6, 1.6);
      // Shaft, coupling guard.
      gr = g.createLinearGradient(0, 176, 0, 196);
      gr.addColorStop(0, '#d8dee6'); gr.addColorStop(0.5, '#ffffff'); gr.addColorStop(1, '#7d8794');
      g.fillStyle = gr; g.fillRect(360, 176, 40, 20);
      gr = g.createLinearGradient(0, 150, 0, 222);
      gr.addColorStop(0, '#e6a640'); gr.addColorStop(0.35, '#ffd27a'); gr.addColorStop(1, '#8a5a12');
      g.fillStyle = gr; rr(g, 398, 150, 44, 72, 8); g.fill();
      g.restore();
      // Pump (fixed to the base, larger and steadier).
      gr = g.createLinearGradient(0, 128, 0, 262);
      gr.addColorStop(0, '#3e4b5c'); gr.addColorStop(0.3, '#71839a'); gr.addColorStop(1, '#232a33');
      g.fillStyle = gr; g.beginPath(); g.ellipse(505, 190, 62, 70, 0, 0, 6.3); g.fill();
      g.fillStyle = '#2a3139'; rr(g, 470, 236, 70, 28, 4); g.fill();
      g.strokeStyle = 'rgba(200,214,232,.35)'; g.lineWidth = 3; g.beginPath(); g.ellipse(505, 190, 44, 50, 0, 0, 6.3); g.stroke();
      gr = g.createLinearGradient(0, 70, 0, 128);
      gr.addColorStop(0, '#59687b'); gr.addColorStop(1, '#2c343e');
      g.fillStyle = gr; g.fillRect(488, 70, 34, 62);
      g.fillStyle = '#6c7b8e'; g.fillRect(478, 64, 54, 10);
      // Lighting and lens vignette.
      gr = g.createRadialGradient(W * 0.45, H * 0.45, H * 0.2, W * 0.5, H * 0.5, W * 0.7);
      gr.addColorStop(0, 'rgba(0,0,0,0)'); gr.addColorStop(1, 'rgba(0,0,0,.55)');
      g.fillStyle = gr; g.fillRect(0, 0, W, H);
      // Sensor noise.
      const img = g.getImageData(0, 0, W, H), d = img.data;
      for (let i = 0; i < d.length; i += 4) { const n = (Math.random() - 0.5) * 10; d[i] += n; d[i + 1] += n; d[i + 2] += n; }
      g.putImageData(img, 0, 0);
      return c.toDataURL('image/jpeg', 0.86);
      function rr(g2, x, y, w, h, r) { g2.beginPath(); g2.moveTo(x + r, y); g2.arcTo(x + w, y, x + w, y + h, r); g2.arcTo(x + w, y + h, x, y + h, r); g2.arcTo(x, y + h, x, y, r); g2.arcTo(x, y, x + w, y, r); g2.closePath(); }
    }
    fileUrl() { return ''; }
    async fetchText() { return null; }
  }
  window.DemoDevice = DemoDevice;
})();
