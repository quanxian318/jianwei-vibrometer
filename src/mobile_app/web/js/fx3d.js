// Lightweight 3D on a 2D canvas (no WebGL, works offline in the WebView):
//  * Orb       – particle sphere breathing with the measured frequency/amplitude;
//  * Waterfall – time-frequency surface (short-time FFT of a waveform);
//  * DeviceModel – rotating wireframe of the instrument (connect page);
//  * tilt()    – perspective tilt of cards under the finger.
(function () {
  const css = (n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  function rgb(hex) {
    const h = hex.replace('#', '');
    const n = parseInt(h.length === 3 ? h.split('').map((c) => c + c).join('') : h, 16);
    return [n >> 16 & 255, n >> 8 & 255, n & 255];
  }
  function mix(a, b, t) { return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t]; }
  function fit(c) {
    const dpr = Math.min(2.5, window.devicePixelRatio || 1);
    const W = c.clientWidth, H = c.clientHeight;
    if (c.width !== Math.round(W * dpr) || c.height !== Math.round(H * dpr)) { c.width = Math.round(W * dpr); c.height = Math.round(H * dpr); }
    const g = c.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { g, W, H };
  }
  // Rotation about Y then X, perspective projection.
  function project(p, ry, rx, f, cx, cy) {
    const cyR = Math.cos(ry), syR = Math.sin(ry), cxR = Math.cos(rx), sxR = Math.sin(rx);
    const x = p[0] * cyR + p[2] * syR;
    let z = -p[0] * syR + p[2] * cyR;
    const y = p[1] * cxR - z * sxR;
    z = p[1] * sxR + z * cxR;
    const s = f / (f + z);
    return [cx + x * s, cy + y * s, z, s];
  }

  class Loop {
    constructor(canvas) {
      this.c = canvas; this.run = false; this.t0 = performance.now();
      this.vis = true;
      if (typeof IntersectionObserver !== 'undefined') {
        this.io = new IntersectionObserver((e) => { this.vis = e[0].isIntersecting; if (this.vis) this.start(); });
        this.io.observe(canvas);
      }
    }
    start() {
      if (this.run) return;
      this.run = true;
      const step = (now) => {
        if (!this.run) return;
        if (!this.vis || document.hidden || !this.c.isConnected || !this.c.offsetParent) { this.run = false; return; }
        this.frame((now - this.t0) / 1000);
        requestAnimationFrame(step);
      };
      requestAnimationFrame(step);
    }
    stop() { this.run = false; }
  }

  // ------------------------------------------------------------------ Orb
  class Orb extends Loop {
    constructor(canvas) {
      super(canvas);
      this.n = 900;
      this.pts = [];
      const g = Math.PI * (3 - Math.sqrt(5));  // Fibonacci sphere
      for (let i = 0; i < this.n; i++) {
        const y = 1 - (i / (this.n - 1)) * 2, r = Math.sqrt(1 - y * y), th = g * i;
        this.pts.push([Math.cos(th) * r, y, Math.sin(th) * r, Math.random() * 6.28]);
      }
      this.hz = 0; this.amp = 0; this.level = 'unknown'; this.active = false; this.progress = 0;
      this.drag = 0;
      let lx = null;
      canvas.addEventListener('touchstart', (e) => { lx = e.touches[0].clientX; }, { passive: true });
      canvas.addEventListener('touchmove', (e) => { const x = e.touches[0].clientX; if (lx != null) this.drag += (x - lx) * 0.012; lx = x; }, { passive: true });
      canvas.addEventListener('touchend', () => { lx = null; });
    }
    set(o) { Object.assign(this, o); this.start(); }
    frame(t) {
      const { g, W, H } = fit(this.c);
      g.clearRect(0, 0, W, H);
      const cx = W / 2, cy = H / 2, R = Math.min(W, H) * 0.3;
      const dark = document.documentElement.getAttribute('data-theme') === 'dark';
      const lvc = this.level === 'alarm' ? css('--alarm') : this.level === 'warn' ? css('--warn') : this.level === 'normal' ? css('--ok') : css('--accent');
      const c1 = rgb(css('--accent')), c2 = rgb(css('--accent-2')), c3 = rgb(lvc);
      // Visual rate: the measured frequency folded into a watchable range.
      const f = this.active ? 1.6 : this.hz > 0 ? 0.5 + (Math.log2(1 + this.hz) % 1.5) : 0.4;
      const a = this.active ? 0.13 : U.clamp(this.amp, 0.04, 0.2);
      const ry = t * 0.22 + this.drag, rx = -0.35 + Math.sin(t * 0.3) * 0.08;
      const proj = [];
      for (let i = 0; i < this.n; i++) {
        const p = this.pts[i];
        // Two travelling ripples over the sphere.
        const w = Math.sin(t * f * Math.PI * 2 - p[1] * 6) * 0.65 + Math.sin(t * f * 4.4 + Math.atan2(p[2], p[0]) * 3) * 0.35;
        const r = R * (1 + a * w);
        const q = project([p[0] * r, p[1] * r, p[2] * r], ry, rx, R * 4, cx, cy);
        q.push(w, p[3]);
        proj.push(q);
      }
      proj.sort((u, v) => v[2] - u[2]);
      g.globalCompositeOperation = dark ? 'lighter' : 'source-over';
      for (const q of proj) {
        const depth = U.clamp((R - q[2]) / (2 * R), 0, 1);  // 1 = front
        // Fresnel-like: points near the silhouette glow; the centre stays clear for the reading.
        const dx = (q[0] - cx) / R, dy = (q[1] - cy) / R, rr = Math.sqrt(dx * dx + dy * dy);
        const rim = U.clamp((rr - 0.45) / 0.55, 0, 1);
        const tw = 0.65 + 0.35 * Math.sin(t * 3 + q[5]);
        const wv = (q[4] + 1) / 2;
        const c = wv > 0.72 ? mix(c2, c3, (wv - 0.72) / 0.28) : mix(c1, c2, wv / 0.72);
        const al = (0.12 + 0.88 * rim) * (0.25 + 0.75 * depth) * tw * (dark ? 0.95 : 0.85);
        const size = (0.7 + 1.5 * depth + 1.2 * Math.max(0, q[4]) * rim) * q[3];
        g.fillStyle = 'rgba(' + c.map(Math.round).join(',') + ',' + al.toFixed(3) + ')';
        g.beginPath(); g.arc(q[0], q[1], size, 0, Math.PI * 2); g.fill();
      }
      g.globalCompositeOperation = 'source-over';
      // Halo.
      const halo = g.createRadialGradient(cx, cy, R * 0.85, cx, cy, R * 1.35);
      halo.addColorStop(0, 'rgba(' + c1.join(',') + ',' + (dark ? 0.16 : 0.10) + ')');
      halo.addColorStop(1, 'rgba(' + c1.join(',') + ',0)');
      g.fillStyle = halo; g.beginPath(); g.arc(cx, cy, R * 1.35, 0, Math.PI * 2); g.fill();
      if (this.active) {  // progress satellite on an orbit
        const th = this.progress * Math.PI * 2 - Math.PI / 2;
        const q = project([Math.cos(th) * 1.22 * R, Math.sin(th) * 1.22 * R * 0.25, Math.sin(th) * 1.22 * R], ry * 0.2, -0.2, R * 4, cx, cy);
        g.fillStyle = css('--accent'); g.shadowColor = css('--accent'); g.shadowBlur = 12;
        g.beginPath(); g.arc(q[0], q[1], 4 * q[3], 0, Math.PI * 2); g.fill(); g.shadowBlur = 0;
      }
    }
  }

  // ------------------------------------------------------------------ STFT
  function fftMag(re) {
    const n = re.length, im = new Float64Array(n);
    for (let i = 1, j = 0; i < n; i++) {
      let bit = n >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) { const t = re[i]; re[i] = re[j]; re[j] = t; }
    }
    for (let len = 2; len <= n; len <<= 1) {
      const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
      for (let i = 0; i < n; i += len) {
        let cr = 1, ci = 0;
        for (let k = 0; k < len / 2; k++) {
          const ar = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci, ai = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
          re[i + k + len / 2] = re[i + k] - ar; im[i + k + len / 2] = im[i + k] - ai;
          re[i + k] += ar; im[i + k] += ai;
          const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
        }
      }
    }
    const out = new Float64Array(n / 2);
    for (let k = 0; k < n / 2; k++) out[k] = Math.hypot(re[k], im[k]) * 2 / n;
    return out;
  }
  // Short-time spectra of a uniformly sampled waveform: rows over time.
  function stft(t, v, maxRows) {
    if (!t || t.length < 64) return null;
    const dt = (t[t.length - 1] - t[0]) / (t.length - 1);
    if (!(dt > 0)) return null;
    let n = 64;
    while (n * 2 <= Math.min(256, t.length / 2)) n *= 2;
    const rows = Math.min(maxRows || 28, Math.max(6, Math.floor((v.length - n) / (n / 4)) + 1));
    const hop = Math.max(1, Math.floor((v.length - n) / Math.max(1, rows - 1)));
    const out = [];
    for (let r = 0; r < rows; r++) {
      const s = r * hop;
      if (s + n > v.length) break;
      let mean = 0;
      for (let i = 0; i < n; i++) mean += v[s + i];
      mean /= n;
      const buf = new Float64Array(n);
      for (let i = 0; i < n; i++) buf[i] = (v[s + i] - mean) * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / (n - 1))) * 2;
      out.push({ t: t[s] + n * dt / 2, mag: fftMag(buf) });
    }
    return { rows: out, df: 1 / (n * dt), n };
  }

  // ------------------------------------------------------------------ Waterfall
  class Waterfall extends Loop {
    constructor(canvas) {
      super(canvas);
      this.ry = -0.55; this.rx = 0.62; this.auto = true; this.data = null;
      let lx = null, ly = null;
      const down = (x, y) => { lx = x; ly = y; this.auto = false; };
      const move = (x, y) => {
        if (lx == null) return;
        this.ry += (x - lx) * 0.01; this.rx = U.clamp(this.rx + (y - ly) * 0.006, 0.15, 1.2);
        lx = x; ly = y; this.start();
      };
      canvas.addEventListener('touchstart', (e) => down(e.touches[0].clientX, e.touches[0].clientY), { passive: true });
      canvas.addEventListener('touchmove', (e) => move(e.touches[0].clientX, e.touches[0].clientY), { passive: true });
      canvas.addEventListener('touchend', () => { lx = null; });
      canvas.addEventListener('mousedown', (e) => down(e.clientX, e.clientY));
      window.addEventListener('mousemove', (e) => move(e.clientX, e.clientY));
      window.addEventListener('mouseup', () => { lx = null; });
    }
    set(t, v, fMax) {
      const s = stft(t, v, 30);
      if (!s) { this.data = null; this.start(); return; }
      const bins = Math.min(s.rows[0].mag.length, Math.max(8, Math.ceil((fMax || Infinity) / s.df)));
      let peak = 1e-12;
      s.rows.forEach((r) => { for (let k = 1; k < bins; k++) peak = Math.max(peak, r.mag[k]); });
      this.data = { rows: s.rows, bins, df: s.df, peak };
      this.start();
    }
    frame(t) {
      const { g, W, H } = fit(this.c);
      g.clearRect(0, 0, W, H);
      const d = this.data;
      if (!d) { g.fillStyle = css('--text-3'); g.font = '13px system-ui'; g.textAlign = 'center'; g.fillText('样本不足，无法生成时频图', W / 2, H / 2); this.stop(); return; }
      if (this.auto) this.ry = -0.55 + Math.sin(t * 0.35) * 0.32;
      const cx = W / 2, cy = H * 0.6, S = Math.min(W * 0.32, H * 0.6);
      const rows = d.rows.length, bins = d.bins;
      const P = (fx, ty, z) => project([(fx - 0.5) * 2 * S, -z * S * 0.85, (ty - 0.5) * 1.5 * S], this.ry, this.rx, S * 5, cx, cy);
      const c0 = rgb(css('--accent-2')), c1 = rgb(css('--accent')), c2 = rgb(css('--warn'));
      const colorAt = (z) => (z < 0.5 ? mix(c0, c1, z * 2) : mix(c1, c2, (z - 0.5) * 2));
      // Floor grid.
      g.lineWidth = 1; g.strokeStyle = css('--chart-grid');
      for (let i = 0; i <= 6; i++) {
        let a = P(i / 6, 0, 0), b = P(i / 6, 1, 0); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke();
        a = P(0, i / 6, 0); b = P(1, i / 6, 0); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke();
      }
      // Rows back to front so nearer ridges cover farther ones.
      const order = [];
      for (let r = 0; r < rows; r++) order.push(r);
      const front = Math.cos(this.ry) >= 0;
      if (!front) order.reverse();
      const bg = css('--bg');
      order.forEach((r) => {
        const row = d.rows[r], ty = rows > 1 ? r / (rows - 1) : 0.5;
        const pts = [];
        let zmax = 0;
        for (let k = 0; k < bins; k++) {
          const z = Math.sqrt(row.mag[k] / d.peak);  // sqrt: small lines stay visible
          zmax = Math.max(zmax, z);
          pts.push(P(k / (bins - 1), ty, Math.min(1, z)));
        }
        const base0 = P(0, ty, 0), base1 = P(1, ty, 0);
        g.beginPath(); g.moveTo(base0[0], base0[1]);
        pts.forEach((p) => g.lineTo(p[0], p[1]));
        g.lineTo(base1[0], base1[1]); g.closePath();
        g.fillStyle = bg; g.globalAlpha = 0.86; g.fill(); g.globalAlpha = 1;
        const gr = g.createLinearGradient(0, base0[1], 0, base0[1] - S * 0.85);
        const ca = colorAt(0.1), cb = colorAt(Math.min(1, zmax));
        gr.addColorStop(0, 'rgba(' + ca.map(Math.round).join(',') + ',0.05)');
        gr.addColorStop(1, 'rgba(' + cb.map(Math.round).join(',') + ',0.35)');
        g.fillStyle = gr; g.fill();
        g.beginPath();
        pts.forEach((p, i) => (i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1])));
        // Line colour by height: blue at the floor, teal, amber at the peak.
        const top = P(0.5, ty, 1)[1];
        const lg = g.createLinearGradient(0, base0[1], 0, top);
        lg.addColorStop(0, 'rgb(' + c0.join(',') + ')'); lg.addColorStop(0.45, 'rgb(' + c1.join(',') + ')'); lg.addColorStop(1, 'rgb(' + c2.join(',') + ')');
        g.strokeStyle = lg; g.lineWidth = 1.3; g.stroke();
      });
      // Axis labels.
      g.fillStyle = css('--chart-axis'); g.font = '10.5px system-ui'; g.textAlign = 'center';
      const fmax = d.df * (bins - 1);
      [0, 0.5, 1].forEach((u) => { const p = P(u, front ? 1.06 : -0.06, 0); g.fillText((fmax * u).toFixed(fmax * u < 10 ? 1 : 0) + ' Hz', p[0], p[1] + 12); });
      if (!this.auto) this.stop();
    }
  }

  // ------------------------------------------------------------------ device model
  // Instrument as a wireframe: housing, the stereo camera pair, the lamp bars.
  class DeviceModel extends Loop {
    constructor(canvas) {
      super(canvas);
      const box = (x0, y0, z0, x1, y1, z1) => {
        const v = [[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0], [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]];
        const e = [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]];
        return e.map((p) => [v[p[0]], v[p[1]]]);
      };
      const ring = (cx, cy, z, r, n) => {
        const out = [];
        for (let i = 0; i < n; i++) {
          const a = i / n * Math.PI * 2, b = (i + 1) / n * Math.PI * 2;
          out.push([[cx + Math.cos(a) * r, cy + Math.sin(a) * r, z], [cx + Math.cos(b) * r, cy + Math.sin(b) * r, z]]);
        }
        return out;
      };
      this.edges = [].concat(
        box(-1.3, -0.75, -0.5, 1.3, 0.75, 0.5),            // housing
        box(-1.1, -0.55, -0.62, 1.1, 0.55, -0.5),          // screen bezel (back)
        ring(-0.42, -0.1, 0.5, 0.26, 24), ring(-0.42, -0.1, 0.5, 0.14, 18),
        ring(0.42, -0.1, 0.5, 0.26, 24), ring(0.42, -0.1, 0.5, 0.14, 18),
        box(-1.05, 0.38, 0.5, -0.75, 0.52, 0.56), box(0.75, 0.38, 0.5, 1.05, 0.52, 0.56),  // lamps
        box(-0.12, 0.42, 0.5, 0.12, 0.5, 0.58)             // cross projector
      );
      this.accentEdges = new Set();
      for (let i = 24; i < 24 + 84; i++) this.accentEdges.add(i);
    }
    frame(t) {
      const { g, W, H } = fit(this.c);
      g.clearRect(0, 0, W, H);
      const cx = W / 2, cy = H / 2, S = Math.min(W, H) * 0.26;
      const ry = Math.sin(t * 0.5) * 0.7 + 0.25, rx = -0.25 + Math.sin(t * 0.37) * 0.08;
      const a = rgb(css('--accent')), b = rgb(css('--text-3'));
      // Measuring beam from the lenses to a vibrating target.
      const tgtZ = 3.2, wob = Math.sin(t * 2 * Math.PI * 1.1) * 0.12;
      const tgt = [[-0.9, -0.6 + wob, tgtZ], [0.9, -0.6 + wob, tgtZ], [0.9, 0.4 + wob, tgtZ], [-0.9, 0.4 + wob, tgtZ]];
      const pr = (p) => project([p[0] * S, p[1] * S, -p[2] * S], ry, rx, S * 7, cx, cy);
      g.lineWidth = 1;
      [[-0.42, -0.1], [0.42, -0.1]].forEach((l) => {
        const o = pr([l[0], l[1], 0.5]);
        tgt.forEach((p) => { const q = pr(p); const gr = g.createLinearGradient(o[0], o[1], q[0], q[1]); gr.addColorStop(0, 'rgba(' + a.join(',') + ',0.35)'); gr.addColorStop(1, 'rgba(' + a.join(',') + ',0)'); g.strokeStyle = gr; g.beginPath(); g.moveTo(o[0], o[1]); g.lineTo(q[0], q[1]); g.stroke(); });
      });
      g.beginPath(); tgt.forEach((p, i) => { const q = pr(p); if (i) g.lineTo(q[0], q[1]); else g.moveTo(q[0], q[1]); }); g.closePath();
      g.strokeStyle = 'rgba(' + a.join(',') + ',0.5)'; g.setLineDash([3, 4]); g.stroke(); g.setLineDash([]);
      this.edges.forEach((e, i) => {
        const p = pr(e[0]), q = pr(e[1]);
        const depth = U.clamp(0.5 - (p[2] + q[2]) / (4 * S), 0.15, 1);
        const c = this.accentEdges.has(i) ? a : b;
        g.strokeStyle = 'rgba(' + c.join(',') + ',' + depth.toFixed(2) + ')';
        g.lineWidth = this.accentEdges.has(i) ? 1.5 : 1.2;
        g.beginPath(); g.moveTo(p[0], p[1]); g.lineTo(q[0], q[1]); g.stroke();
      });
    }
  }

  // ------------------------------------------------------------------ tilt
  function tilt(root) {
    root.addEventListener('pointerdown', (e) => {
      const card = e.target.closest('[data-tilt]');
      if (!card) return;
      const r = card.getBoundingClientRect();
      const x = (e.clientX - r.left) / r.width - 0.5, y = (e.clientY - r.top) / r.height - 0.5;
      card.style.transition = 'transform .18s var(--ease)';
      card.style.transform = 'perspective(700px) rotateX(' + (-y * 6).toFixed(2) + 'deg) rotateY(' + (x * 8).toFixed(2) + 'deg) scale(.985)';
      const up = () => { card.style.transform = ''; window.removeEventListener('pointerup', up); window.removeEventListener('pointercancel', up); };
      window.addEventListener('pointerup', up); window.addEventListener('pointercancel', up);
    });
  }

  window.FX3D = { Orb, Waterfall, DeviceModel, tilt, stft };
})();
