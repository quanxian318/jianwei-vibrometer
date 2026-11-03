// Canvas charts: HiDPI line/area plots with a gradient fill, light grid, peak
// markers and a touch crosshair. Colours come from the CSS theme tokens.
(function () {
  function css(name) { return getComputedStyle(document.documentElement).getPropertyValue(name).trim(); }
  function niceStep(range, target) {
    const raw = range / Math.max(1, target);
    const mag = Math.pow(10, Math.floor(Math.log10(raw || 1)));
    const n = raw / mag;
    return (n < 1.5 ? 1 : n < 3 ? 2 : n < 7 ? 5 : 10) * mag;
  }
  function fmt(v, step) {
    const d = step >= 1 ? 0 : Math.min(4, Math.ceil(-Math.log10(step)));
    return v.toFixed(d);
  }
  function hexA(hex, a) {
    if (!hex || hex[0] !== '#') return hex;
    const n = parseInt(hex.length === 4 ? hex.replace(/#(.)(.)(.)/, '#$1$1$2$2$3$3').slice(1) : hex.slice(1), 16);
    return 'rgba(' + (n >> 16 & 255) + ',' + (n >> 8 & 255) + ',' + (n & 255) + ',' + a + ')';
  }

  class Chart {
    constructor(canvas, opts) {
      this.c = canvas;
      this.o = Object.assign({ padL: 40, padR: 10, padT: 12, padB: 24, xTicks: 5, yTicks: 4, fill: true, grid: true, axes: true }, opts || {});
      this.data = null;
      this.cross = null;
      if (this.o.interactive) this.bindTouch();
      this.ro = typeof ResizeObserver !== 'undefined' ? new ResizeObserver(() => this.draw()) : null;
      if (this.ro) this.ro.observe(canvas);
    }
    destroy() { if (this.ro) this.ro.disconnect(); }
    set(data) { this.data = data; this.draw(); }
    bindTouch() {
      const move = (e) => {
        const r = this.c.getBoundingClientRect();
        const p = e.touches ? e.touches[0] : e;
        this.cross = (p.clientX - r.left) / r.width;
        this.draw();
      };
      const end = () => { this.cross = null; this.draw(); if (this.o.onReadout) this.o.onReadout(null); };
      this.c.addEventListener('touchstart', move, { passive: true });
      this.c.addEventListener('touchmove', move, { passive: true });
      this.c.addEventListener('touchend', end);
      this.c.addEventListener('mousemove', move);
      this.c.addEventListener('mouseleave', end);
    }
    draw() {
      const c = this.c;
      const dpr = window.devicePixelRatio || 1;
      const W = c.clientWidth, H = c.clientHeight;
      if (!W || !H) return;
      if (c.width !== Math.round(W * dpr) || c.height !== Math.round(H * dpr)) { c.width = Math.round(W * dpr); c.height = Math.round(H * dpr); }
      const g = c.getContext('2d');
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, W, H);
      const d = this.data;
      if (!d || !d.series || !d.series.length || !d.series[0].y.length) {
        g.fillStyle = css('--text-3'); g.font = '13px system-ui'; g.textAlign = 'center';
        g.fillText(d && d.empty ? d.empty : '暂无数据', W / 2, H / 2);
        return;
      }
      const o = this.o;
      const L = o.axes ? o.padL : 4, R = W - (o.axes ? o.padR : 4), T = o.axes ? o.padT : 4, B = H - (o.axes ? o.padB : 4);
      let x0 = d.xMin, x1 = d.xMax, y0 = d.yMin, y1 = d.yMax;
      const all = d.series;
      if (x0 == null || x1 == null) {
        x0 = Infinity; x1 = -Infinity;
        all.forEach((s) => { const xs = s.x; if (xs.length) { x0 = Math.min(x0, xs[0]); x1 = Math.max(x1, xs[xs.length - 1]); } });
      }
      if (y0 == null || y1 == null) {
        let lo = Infinity, hi = -Infinity;
        all.forEach((s) => s.y.forEach((v) => { if (isFinite(v)) { lo = Math.min(lo, v); hi = Math.max(hi, v); } }));
        if (d.symmetric) { const m = Math.max(Math.abs(lo), Math.abs(hi)) || 1; lo = -m; hi = m; }
        if (d.zeroBase) lo = Math.min(0, lo);
        if (hi === lo) { hi += 1; lo -= 1; }
        const pad = (hi - lo) * 0.08;
        y0 = d.zeroBase ? lo : lo - pad; y1 = hi + pad;
      }
      if (x1 === x0) x1 = x0 + 1;
      const X = (v) => L + (v - x0) / (x1 - x0) * (R - L);
      const Y = (v) => B - (v - y0) / (y1 - y0) * (B - T);
      g.font = '10.5px system-ui';
      g.lineWidth = 1;
      if (o.grid) {
        const ys = niceStep(y1 - y0, o.yTicks);
        g.strokeStyle = css('--chart-grid'); g.fillStyle = css('--chart-axis'); g.textAlign = 'right'; g.textBaseline = 'middle';
        for (let v = Math.ceil(y0 / ys) * ys; v <= y1 + 1e-9; v += ys) {
          const y = Math.round(Y(v)) + 0.5;
          g.beginPath(); g.moveTo(L, y); g.lineTo(R, y); g.stroke();
          if (o.axes) g.fillText(fmt(v, ys), L - 6, y);
        }
        if (o.axes) {
          const xs = d.xInt ? Math.max(1, niceStep(x1 - x0, o.xTicks)) : niceStep(x1 - x0, o.xTicks);
          g.textAlign = 'center'; g.textBaseline = 'top';
          for (let v = Math.ceil(x0 / xs) * xs; v <= x1 + 1e-9; v += xs) g.fillText(fmt(v, xs) + (d.xUnit || ''), X(v), B + 7);
        }
      }
      (d.bands || []).forEach((b) => {
        g.fillStyle = b.color;
        g.fillRect(L, Y(Math.min(y1, b.to)), R - L, Y(Math.max(y0, b.from)) - Y(Math.min(y1, b.to)));
      });
      (d.hlines || []).forEach((h) => {
        if (h.y < y0 || h.y > y1) return;
        g.save(); g.setLineDash([4, 4]); g.strokeStyle = h.color; g.beginPath(); g.moveTo(L, Y(h.y)); g.lineTo(R, Y(h.y)); g.stroke(); g.restore();
        if (h.label) { g.fillStyle = h.color; g.textAlign = 'right'; g.textBaseline = 'bottom'; g.fillText(h.label, R - 2, Y(h.y) - 2); }
      });
      all.forEach((s, si) => {
        const color = s.color || css(si ? '--accent-2' : '--accent');
        g.save();
        g.beginPath(); g.rect(L, T - 2, R - L, B - T + 4); g.clip();
        g.beginPath();
        const n = s.y.length;
        for (let i = 0; i < n; i++) { const px = X(s.x[i]), py = Y(s.y[i]); if (i) g.lineTo(px, py); else g.moveTo(px, py); }
        g.lineJoin = 'round'; g.lineCap = 'round';
        g.strokeStyle = color; g.lineWidth = s.width || 1.8; g.stroke();
        if (o.fill && s.fill !== false) {
          const base = d.symmetric ? Y(0) : B;
          g.lineTo(X(s.x[n - 1]), base); g.lineTo(X(s.x[0]), base); g.closePath();
          const gr = g.createLinearGradient(0, T, 0, B);
          gr.addColorStop(0, hexA(color, 0.28)); gr.addColorStop(1, hexA(color, 0));
          g.fillStyle = gr; g.fill();
        }
        if (s.dots) {
          for (let i = 0; i < n; i++) {
            g.beginPath(); g.arc(X(s.x[i]), Y(s.y[i]), 3.2, 0, Math.PI * 2);
            g.fillStyle = s.dotColors ? s.dotColors[i] : color; g.fill();
            g.lineWidth = 2; g.strokeStyle = css('--bg'); g.stroke();
          }
        }
        g.restore();
      });
      (d.markers || []).forEach((m) => {
        const px = X(m.x), py = Y(m.y);
        g.fillStyle = m.color || css('--text'); g.beginPath(); g.arc(px, py, 4, 0, Math.PI * 2); g.fill();
        g.strokeStyle = css('--bg'); g.lineWidth = 2; g.stroke();
        if (m.label) {
          g.font = '600 11.5px system-ui';
          const tw = g.measureText(m.label).width + 14;
          let bx = Math.min(R - tw, Math.max(L, px - tw / 2));
          const by = Math.max(T, py - 28);
          g.fillStyle = css('--surface-solid'); g.strokeStyle = css('--border-strong'); g.lineWidth = 1;
          roundRect(g, bx, by, tw, 20, 7); g.fill(); g.stroke();
          g.fillStyle = css('--text'); g.textAlign = 'left'; g.textBaseline = 'middle'; g.fillText(m.label, bx + 7, by + 10);
        }
      });
      if (this.cross != null) {
        const s = all[0];
        const xv = x0 + U.clamp(this.cross, 0, 1) * (x1 - x0);
        let bi = 0, bd = Infinity;
        for (let i = 0; i < s.x.length; i++) { const dd = Math.abs(s.x[i] - xv); if (dd < bd) { bd = dd; bi = i; } }
        const px = X(s.x[bi]), py = Y(s.y[bi]);
        g.strokeStyle = css('--text-3'); g.lineWidth = 1; g.beginPath(); g.moveTo(px, T); g.lineTo(px, B); g.stroke();
        g.fillStyle = css('--accent'); g.beginPath(); g.arc(px, py, 4.5, 0, Math.PI * 2); g.fill();
        g.strokeStyle = css('--bg'); g.lineWidth = 2; g.stroke();
        if (this.o.onReadout) this.o.onReadout({ x: s.x[bi], y: s.y[bi], i: bi });
      }
    }
  }
  function roundRect(g, x, y, w, h, r) {
    g.beginPath(); g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r);
    g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath();
  }
  // Inline SVG sparkline (record and point lists).
  function spark(values, color, w, h) {
    w = w || 84; h = h || 30;
    const v = values.filter((x) => isFinite(x));
    if (v.length < 2) return '<svg width="' + w + '" height="' + h + '"></svg>';
    const lo = Math.min.apply(null, v), hi = Math.max.apply(null, v);
    const pts = v.map((x, i) => (i / (v.length - 1) * (w - 4) + 2).toFixed(1) + ',' + (h - 3 - (hi === lo ? 0.5 : (x - lo) / (hi - lo)) * (h - 6)).toFixed(1));
    return '<svg width="' + w + '" height="' + h + '" viewBox="0 0 ' + w + ' ' + h + '"><polyline points="' + pts.join(' ') +
      '" fill="none" stroke="' + color + '" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>' +
      '<circle cx="' + pts[pts.length - 1].split(',')[0] + '" cy="' + pts[pts.length - 1].split(',')[1] + '" r="2.6" fill="' + color + '"/></svg>';
  }
  window.Charts = { Chart, spark, css };
})();
