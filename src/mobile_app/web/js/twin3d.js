// Digital twin of the instrument: a product-grade 3D model drawn on a 2D canvas
// (no WebGL, works offline). Rounded, chamfered housing; glass, metal, PCB and
// screen materials with specular light; decals for the live screen, PCB traces,
// grilles and lens glass; a soft floor shadow. Drag to rotate (with inertia),
// pinch/wheel to zoom, setExplode(0..1) spreads the modules, tap a module to
// select it. Status (ok/warn/alarm/off/unknown) comes from page_device.js.
(function () {
  const css = (n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  const rgb = (hex) => {
    const h = (hex || '#888').replace('#', '');
    const n = parseInt(h.length === 3 ? h.split('').map((c) => c + c).join('') : h, 16);
    return [n >> 16 & 255, n >> 8 & 255, n & 255];
  };
  const norm = (v) => { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; };

  // ------------------------------------------------------------ geometry
  // Faces: {p: [[x,y,z]...], n: [x,y,z], mat, decal?}. Model space in cm:
  // x right, y down, z towards the lens side (front).
  function rrProfile(w, h, r, seg) {
    const pts = [];
    const cs = [[w / 2 - r, h / 2 - r, 0], [-w / 2 + r, h / 2 - r, Math.PI / 2], [-w / 2 + r, -h / 2 + r, Math.PI], [w / 2 - r, -h / 2 + r, Math.PI * 1.5]];
    cs.forEach((c) => { for (let i = 0; i <= seg; i++) { const a = c[2] + i / seg * Math.PI / 2; pts.push([c[0] + Math.cos(a) * r, c[1] + Math.sin(a) * r]); } });
    return pts;
  }
  // Rounded-rectangle prism along z with chamfered caps (bevel b at each end).
  function rbox(cx, cy, cz, w, h, d, r, b, mat, capMat, seg) {
    seg = seg || 6;
    b = Math.min(b || 0, r * 0.8, d / 3);
    const out = [], P = rrProfile(w, h, r, seg), Q = b ? rrProfile(w - 2 * b, h - 2 * b, Math.max(0.05, r - b), seg) : P;
    const z0 = cz - d / 2, z1 = cz + d / 2;
    const at = (p, z) => [cx + p[0], cy + p[1], z];
    for (let i = 0; i < P.length; i++) {
      const a = P[i], c = P[(i + 1) % P.length];
      const n = norm([c[1] - a[1], -(c[0] - a[0]), 0]);
      out.push({ p: [at(a, z0 + b), at(c, z0 + b), at(c, z1 - b), at(a, z1 - b)], n, mat });
      if (b) {
        const qa = Q[i], qc = Q[(i + 1) % Q.length];
        out.push({ p: [at(a, z1 - b), at(c, z1 - b), at(qc, z1), at(qa, z1)], n: norm([n[0], n[1], 1.1]), mat });
        out.push({ p: [at(qa, z0), at(qc, z0), at(c, z0 + b), at(a, z0 + b)], n: norm([n[0], n[1], -1.1]), mat });
      }
    }
    out.push({ p: Q.map((q) => at(q, z1)), n: [0, 0, 1], mat: capMat || mat });
    out.push({ p: Q.slice().reverse().map((q) => at(q, z0)), n: [0, 0, -1], mat: capMat || mat });
    return out;
  }
  function box(cx, cy, cz, w, h, d, mat, decalTop) {
    const x0 = cx - w / 2, x1 = cx + w / 2, y0 = cy - h / 2, y1 = cy + h / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    return [
      { p: [[x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]], n: [0, 0, 1], mat, decal: decalTop },
      { p: [[x1, y0, z0], [x0, y0, z0], [x0, y1, z0], [x1, y1, z0]], n: [0, 0, -1], mat },
      { p: [[x1, y0, z1], [x1, y0, z0], [x1, y1, z0], [x1, y1, z1]], n: [1, 0, 0], mat },
      { p: [[x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0]], n: [-1, 0, 0], mat },
      { p: [[x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1]], n: [0, -1, 0], mat },
      { p: [[x0, y1, z1], [x1, y1, z1], [x1, y1, z0], [x0, y1, z0]], n: [0, 1, 0], mat },
    ];
  }
  // Cylinder along an axis; knurl > 0 ridges the side (the knob).
  function cyl(cx, cy, cz, r, len, axis, mat, opt) {
    opt = opt || {};
    const n = opt.n || 22, knurl = opt.knurl || 0;
    const map = (u, v, w) => (axis === 'z' ? [cx + u, cy + v, cz + w] : axis === 'y' ? [cx + u, cy + w, cz + v] : [cx + w, cy + u, cz + v]);
    const nm = (u, v, w) => (axis === 'z' ? [u, v, w] : axis === 'y' ? [u, w, v] : [w, u, v]);
    const out = [], cap1 = [], cap0 = [];
    for (let i = 0; i < n; i++) {
      const a = i / n * Math.PI * 2, c = (i + 1) / n * Math.PI * 2;
      const ra = r * (knurl && i % 2 ? 1 - knurl : 1), rc = r * (knurl && (i + 1) % 2 ? 1 - knurl : 1);
      const A = [Math.cos(a) * ra, Math.sin(a) * ra], C = [Math.cos(c) * rc, Math.sin(c) * rc];
      out.push({ p: [map(A[0], A[1], -len / 2), map(C[0], C[1], -len / 2), map(C[0], C[1], len / 2), map(A[0], A[1], len / 2)], n: nm(Math.cos((a + c) / 2), Math.sin((a + c) / 2), 0), mat: opt.side || mat });
      cap1.push(map(A[0], A[1], len / 2)); cap0.unshift(map(A[0], A[1], -len / 2));
    }
    out.push({ p: cap1, n: nm(0, 0, 1), mat: opt.cap || mat, decal: opt.decal });
    out.push({ p: cap0, n: nm(0, 0, -1), mat: opt.back || mat, decal: opt.backDecal });
    return out;
  }
  // Flat decal quad facing +z (or -z with back).
  function plate(cx, cy, z, w, h, mat, decal, back) {
    const x0 = cx - w / 2, x1 = cx + w / 2, y0 = cy - h / 2, y1 = cy + h / 2;
    return back ? [{ p: [[x1, y0, z], [x0, y0, z], [x0, y1, z], [x1, y1, z]], n: [0, 0, -1], mat, decal }]
      : [{ p: [[x0, y0, z], [x1, y0, z], [x1, y1, z], [x0, y1, z]], n: [0, 0, 1], mat, decal }];
  }
  function plate3(cx, y, cz, w, d, mat) {
    const x0 = cx - w / 2, x1 = cx + w / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    return [{ p: [[x0, y, z0], [x1, y, z0], [x1, y, z1], [x0, y, z1]], n: [0, -1, 0], mat }];
  }
  function sidePlate(x, cy, cz, h, d, mat, decal, left) {
    const y0 = cy - h / 2, y1 = cy + h / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    return left ? [{ p: [[x, y0, z0], [x, y0, z1], [x, y1, z1], [x, y1, z0]], n: [-1, 0, 0], mat, decal }]
      : [{ p: [[x, y0, z1], [x, y0, z0], [x, y1, z0], [x, y1, z1]], n: [1, 0, 0], mat, decal }];
  }
  const cat = (...a) => [].concat(...a);
  // Fan blades in an x = const plane, turned about (cy, cz) at draw time.
  function blades(cx, cy, cz, r, mat) {
    const out = [];
    for (let i = 0; i < 7; i++) {
      const a = i / 7 * Math.PI * 2;
      const p = (t, s) => [cx, cy + Math.cos(a + s) * t, cz + Math.sin(a + s) * t];
      out.push({ p: [p(0.45, 0.25), p(r, 0.05), p(r, 0.5), p(0.45, 0.55)], n: [-1, 0, 0], mat, spin: [cy, cz] });
    }
    return out;
  }

  // ------------------------------------------------------------ boards
  // A board frame: origin o (model cm, at the board centre on its component
  // side), unit axes ex (board x), ey (board y, image downwards) and en (out of
  // the component side), with ex x ey = en so textures are never mirrored.
  // Faces built in board millimetres (x, y, height) are mapped into the model.
  const frame = (o, ex, ey, en) => ({ o, ex, ey, en });
  const fAt = (F, x, y, z) => frame([0, 1, 2].map((i) => F.o[i] + (F.ex[i] * x + F.ey[i] * y + F.en[i] * z) / 10), F.ex, F.ey, F.en);
  function place(F, faces) {
    const P = (p) => [0, 1, 2].map((i) => F.o[i] + (F.ex[i] * p[0] + F.ey[i] * p[1] + F.en[i] * p[2]) / 10);
    const N = (n) => norm([0, 1, 2].map((i) => F.ex[i] * n[0] + F.ey[i] * n[1] + F.en[i] * n[2]));
    return faces.map((f) => Object.assign({}, f, { p: f.p.map(P), n: N(f.n) }));
  }
  const PART_MAT = { ic: 'chip', housing: 'conn', socket: 'header', shroud: 'header', pins: 'header', metal: 'chrome' };
  const PART_DECAL = { ic: 'chipTop', led: 'led5050', tact: 'tact', socket: 'pins', shroud: 'pins', pins: 'pins', usb: 'usbTop', inductor: 'indTop' };
  // A board: substrate (component side at local z = 0) carrying a texture, and
  // parts {x, y, l, w, h, m, s, a} in millimetres from the board's top-left
  // corner (the same form tools/gen_pcb_twin.py writes for the real boards).
  function pcb(F, w, h, tex, mat, parts) {
    const out = box(0, 0, -0.8, w, h, 1.6, mat, { k: 'tex', tex });
    (parts || []).forEach((p) => {
      const sw = Math.abs(Math.sin((p.a || 0) * Math.PI / 180)) > 0.7;
      const L = sw ? p.w : p.l, Wd = sw ? p.l : p.w, cx = p.x - w / 2, cy = p.y - h / 2;
      const m = PART_MAT[p.m] || p.m;
      if (p.s === 'cyl') out.push(...cyl(cx, cy, p.h / 2, Math.min(L, Wd) / 2, p.h, 'z', m, { n: 18, cap: m + 'Top', decal: p.m === 'coin' ? 'screw' : 'capTop' }));
      else {
        const d = PART_DECAL[p.m];
        out.push(...box(cx, cy, p.h / 2, L, Wd, p.h, m, d === 'pins' ? { k: 'pins', n: Math.max(1, Math.round(Math.max(L, Wd) / 2.54)), r: Math.min(L, Wd) > 4 ? 2 : 1 } : d));
      }
    });
    return place(F, out);
  }
  const REAL = (k) => (window.PCB_BOARDS && window.PCB_BOARDS[k]) || null;
  // Procedural layouts for the bought-in modules (no fabrication files here):
  // the same part list drives pads, silkscreen and the routing in the texture.
  const PROC = {};
  function module3(key, F, w, h, mat, parts) {
    PROC[key] = { w, h, mat, parts };
    return pcb(F, w, h, key, mat, parts);
  }
  const P = (x, y, l, w, h, m, s, a) => ({ x, y, l, w, h, m, s, a });

  // ------------------------------------------------------------ product
  const W = 24, H = 15, D = 7.6, R = 2.8;
  const ZF = D / 2, ZB = -D / 2;
  // Glossy caps get a diagonal sheen.
  const sheen = (faces) => faces.map((f) => (f.mat === 'glass' && f.n[2] > 0.9 ? Object.assign({ decal: 'sheen' }, f) : f));
  // Parts on the outside of the housing: drawn over it while the surface they
  // sit on faces the viewer, hidden while it faces away.
  const FRONT = [0, 0, 1], BACK = [0, 0, -1], TOP = [0, -1, 0], BOTTOM = [0, 1, 0], LEFT = [-1, 0, 0], RIGHT = [1, 0, 0];
  // layer orders parts stacked on the same spot (window < bezel < glass).
  const mount = (n, faces, layer) => faces.map((f) => Object.assign({}, f, { mount: n, layer: layer || 0 }));
  const capDecal = (faces, decal) => faces.map((f) => (Math.abs(f.n[0]) + Math.abs(f.n[1]) < 0.01 && f.n[2] > 0.9 ? Object.assign({}, f, { decal }) : f));
  // Parts inside the closed housing: only drawn once their module moves out.
  const enclosed = (faces) => faces.map((f) => Object.assign({}, f, { inside: true }));
  // Main carrier (real 100 x 100 mm board) faces the lens side; the Nano ESP32
  // sits on its J604/J606 sockets (8.5 mm + 2.5 mm header) as on the real board.
  const CARRIER = frame([4, 0.6, -0.2], [1, 0, 0], [0, 1, 0], [0, 0, 1]);
  // Front panel board (real 60 x 40 mm) under the top face, component side up,
  // status LEDs towards the lens side.
  const PANEL = frame([4, -H / 2 + 0.75, 0], [-1, 0, 0], [0, 0, -1], [0, -1, 0]);
  const panelAt = (x, y) => [PANEL.o[0] - (x - 30) / 10, PANEL.o[2] - (y - 20) / 10];  // board mm -> model x, z
  const FRONTB = REAL('front');
  const fpart = (ref) => (FRONTB ? FRONTB.parts.find((p) => p.ref === ref) : null);
  const LEDS = ['HL101', 'HL102', 'HL103', 'HL104', 'HL105'].map((r, i) => { const p = fpart(r); return p ? panelAt(p.x, p.y) : [1.8 + i * 1.1, 0.7]; });
  const KNOB = (() => { const p = fpart('ENC101'); return p ? panelAt(p.x, p.y) : [2.4, -0.5]; })();
  const KEY = (() => { const p = fpart('K101'); return p ? panelAt(p.x, p.y) : [6, -0.5]; })();
  const MAINB = REAL('main');
  const holes = [[5, 5], [95, 5], [5, 95], [95, 95]];
  const MODULES = (() => [
    { id: 'shellR', shell: true, name: '后壳', parts: cat(
      rbox(0, 0, -1.95, W, H, 3.7, R, 0.7, 'shell', 'shell'),
      // Parting line between the two shells: a narrow recessed groove.
      rbox(0, 0, -0.05, W - 0.36, H - 0.36, 0.14, R - 0.18, 0, 'seam'),
      mount(BOTTOM, cyl(0, H / 2 + 0.18, -1.2, 1.25, 0.36, 'y', 'metal', { n: 18 })),
      mount(LEFT, sidePlate(-W / 2 - 0.01, -1.2, -1.9, 6.2, 2.6, 'grille', 'grille', true)),
      mount(RIGHT, sidePlate(W / 2 + 0.01, 3.4, -1.9, 0.9, 2.2, 'port', 'portC')),
      mount(RIGHT, sidePlate(W / 2 + 0.01, 1.7, -1.9, 0.28, 2.1, 'port')),  // SD card slot
      mount(RIGHT, cyl(W / 2 + 0.04, 5.1, -1.9, 0.56, 0.08, 'x', 'chrome', { n: 20, cap: 'port', decal: 'jack' })),
      // Back cover screws, rubber feet, strap lugs.
      ...[[-10.2, -5.8], [10.2, -5.8], [-10.2, 5.8], [10.2, 5.8]].map((c) => mount(BACK, cyl(c[0], c[1], ZB - 0.03, 0.34, 0.06, 'z', 'screw', { n: 16, back: 'screw', backDecal: 'screw' }))),
      ...[[-8.6, 2.4], [8.6, 2.4], [-8.6, -2.6], [8.6, -2.6]].map((c) => mount(BOTTOM, cyl(c[0], H / 2 + 0.1, c[1], 0.75, 0.2, 'y', 'rubber', { n: 16 }))),
      mount(LEFT, rbox(-W / 2 - 0.22, -4.6, -1.9, 0.44, 1.7, 1.1, 0.2, 0.05, 'metal')),
      mount(RIGHT, rbox(W / 2 + 0.22, -4.6, -1.9, 0.44, 1.7, 1.1, 0.2, 0.05, 'metal'))), ex: [0, 0, -11], delay: 0 },
    { id: 'shellF', shell: true, name: '前壳', parts: cat(
      sheen(rbox(0, 0, 1.95, W, H, 3.7, R, 0.7, 'shell', 'glass')),
      mount(FRONT, plate(-5.2, 3.6, ZF + 0.01, 1.4, 0.5, 'glassHi', 'micHole')),
      mount(FRONT, plate(7.4, -5.3 + 10.6, ZF + 0.01, 3.0, 0.5, 'glass', 'brand'))), ex: [0, 0, 15], delay: 0 },
    { id: 'lcd', short: '触摸屏', name: '7 寸触摸屏', parts: mount(BACK, cat(rbox(0, -0.3, ZB - 0.12, 17.8, 11.2, 0.3, 0.8, 0.08, 'bezel', 'bezel'), plate(0, -0.3, ZB - 0.29, 15.6, 9.2, 'screen', 'screen', true))), ex: [0, 0, -19], delay: 0.05 },
    { id: 'camera', short: '双目相机', name: 'AN5642 双目相机', parts: cat(
      enclosed(module3('cam', frame([0, -1.6, 2.46], [1, 0, 0], [0, 1, 0], [0, 0, 1]), 104, 32, 'pcbDark', [
        P(19, 16, 14, 14, 3.2, 'ic'), P(85, 16, 14, 14, 3.2, 'ic'), P(52, 9, 8, 6, 1.2, 'ic'), P(52, 23, 16, 4, 2, 'housing'),
        P(40, 16, 3, 3, 0.9, 'ic'), P(64, 16, 3, 3, 0.9, 'ic'), P(5, 27, 4, 4, 1, 'ic')])),
      enclosed(cat(cyl(-3.3, -1.6, 3.1, 0.95, 1.3, 'z', 'metal', { n: 24 }), cyl(3.3, -1.6, 3.1, 0.95, 1.3, 'z', 'metal', { n: 24 }))),
      mount(FRONT, capDecal(rbox(0, -1.6, ZF + 0.025, 10.6, 3.6, 0.05, 1.8, 0, 'camWin'), 'sheen'), 0),
      mount(FRONT, cat(cyl(-3.3, -1.6, ZF + 0.09, 1.34, 0.08, 'z', 'chrome', { cap: 'chrome', n: 32 }), cyl(3.3, -1.6, ZF + 0.09, 1.34, 0.08, 'z', 'chrome', { cap: 'chrome', n: 32 })), 1),
      mount(FRONT, cat(cyl(-3.3, -1.6, ZF + 0.14, 1.12, 0.06, 'z', 'lensRing', { n: 32 }), cyl(3.3, -1.6, ZF + 0.14, 1.12, 0.06, 'z', 'lensRing', { n: 32 })), 2),
      mount(FRONT, cat(cyl(-3.3, -1.6, ZF + 0.19, 0.96, 0.05, 'z', 'metal', { cap: 'lens', decal: 'lens', n: 32 }), cyl(3.3, -1.6, ZF + 0.19, 0.96, 0.05, 'z', 'metal', { cap: 'lens', decal: 'lens', n: 32 })), 3)), ex: [0, 0, 9.5], delay: 0.15 },
    { id: 'light', short: '照明', name: '照明灯 LED1', parts: cat(mount(FRONT, cat(rbox(-7.2, -5.2, ZF + 0.08, 4.6, 0.62, 0.16, 0.31, 0, 'lamp'), rbox(7.2, -5.2, ZF + 0.08, 4.6, 0.62, 0.16, 0.31, 0, 'lamp'))),
      mount(FRONT, cat(rbox(-7.2, -5.2, ZF + 0.175, 4.1, 0.22, 0.03, 0.11, 0, 'lampCore'), rbox(7.2, -5.2, ZF + 0.175, 4.1, 0.22, 0.03, 0.11, 0, 'lampCore')), 1)), ex: [0, 0, 15], delay: 0, glow: 'light' },
    { id: 'laser', short: '十字投影', name: '十字投影 LED2', parts: mount(FRONT, cyl(0, -5.2, ZF + 0.08, 0.42, 0.18, 'z', 'chrome', { cap: 'laser' })), ex: [0, 0, 15], delay: 0, glow: 'laser' },
    { id: 'sensors', short: '传感器', name: '环境与测距传感器', parts: cat(
      module3('sens', frame([6.4, 3.4, 2.69], [1, 0, 0], [0, 1, 0], [0, 0, 1]), 30, 22, 'pcbBlue', [P(15, 11, 4.9, 2.5, 1.6, 'ic'), P(6, 11, 2.5, 7.6, 2.5, 'pins'), P(24, 6, 3, 3, 0.8, 'ic'), P(24, 16, 2, 2, 0.8, 'ic')]),
      mount(FRONT, capDecal(rbox(6.4, 3.4, ZF + 0.03, 1.8, 0.8, 0.06, 0.4, 0, 'sensorWin'), 'tof'))), ex: [6, 3, 7.5], delay: 0.3, inner: true },
    { id: 'mic', short: '麦克风', name: 'MP34DT01 麦克风', parts: module3('mic', frame([-5.2, 3.6, 2.68], [1, 0, 0], [0, 1, 0], [0, 0, 1]), 22, 16, 'pcbViolet', [P(11, 8, 4, 3, 1.1, 'metal'), P(3, 8, 2.5, 10.1, 2.5, 'pins'), P(18, 5, 1.6, 0.8, 0.5, 'ic')]), ex: [-6, 3, 7.5], delay: 0.3, inner: true },
    { id: 'nfc', short: 'NFC', name: 'PN532 NFC', parts: cat(box(-8.2, -1.4, 2.6, 4.4, 4.4, 0.14, 'pcbBlue', 'coil')), ex: [-8, -1, 6], delay: 0.25, inner: true },
    { id: 'carrier', short: '主载板', name: '主载板', parts: cat(
      pcb(CARRIER, 100, 100, 'main', 'pcb', MAINB ? MAINB.parts : []),
      // Mounting: brass standoffs behind, screw heads on top.
      place(CARRIER, cat(...holes.map((h) => cat(cyl(h[0] - 50, h[1] - 50, -3.1, 2.4, 3, 'z', 'brass', { n: 6 }), cyl(h[0] - 50, h[1] - 50, 0.8, 2.6, 1.6, 'z', 'screw', { n: 16, cap: 'screw', decal: 'screw' })))))), ex: [0, 0, 3], delay: 0.2, inner: true },
    { id: 'nano', short: 'ESP32', name: 'Nano ESP32 协处理器', parts: module3('nano', fAt(CARRIER, (21.1 + 36.3) / 2 - 50, 37.6 - 50, 12.6), 18, 45, 'pcbTeal', [
      P(9, 3.4, 9, 7.4, 3.2, 'usb'), P(9, 16, 10.4, 14.3, 1.9, 'shield'), P(9, 28, 4, 4, 0.9, 'ic'), P(4.5, 33, 1.6, 1.6, 0.6, 'led'),
      P(13, 33, 3, 2, 0.8, 'ic'), P(9, 39.5, 3.5, 3, 1.4, 'tact')]), ex: [0, 0, 7], delay: 0.35, inner: true },
    { id: 'core', short: 'AX7010', name: 'AX7010 核心板', parts: cat(
      module3('core', frame([-1.6, -0.8, -0.85], [-1, 0, 0], [0, 1, 0], [0, 0, -1]), 104, 68, 'pcb', [
        P(52, 34, 17, 17, 1.4, 'ic'), P(80, 20, 13, 10, 1.2, 'ic'), P(80, 48, 13, 10, 1.2, 'ic'), P(26, 22, 6, 5, 1, 'ic'), P(26, 46, 7, 7, 1, 'ic'),
        P(52, 4, 51, 5, 8.5, 'shroud'), P(52, 64, 51, 5, 8.5, 'shroud'), P(10, 34, 8, 8, 4, 'inductor'), P(96, 34, 6, 6, 3, 'inductor'), P(14, 12, 3, 3, 1, 'ic')]),
      // Heatsink over the Zynq.
      place(frame([-1.6, -0.8, -0.85], [-1, 0, 0], [0, 1, 0], [0, 0, -1]), cat(box(0, 0, 2.4, 22, 22, 2, 'heatsink'),
        ...[0, 1, 2, 3, 4, 5, 6, 7].map((i) => box(-9.6 + i * 2.74, 0, 7.4, 1.2, 22, 8, 'heatsink'))))), ex: [0, 0, -5.5], delay: 0.25, inner: true },
    { id: 'printer', short: '打印机', name: 'EM5820H 打印机', parts: cat(
      rbox(-6.6, 4.4, -1.6, 7.4, 3.6, 3.2, 0.5, 0.1, 'printer', 'printer'),
      cyl(-6.6, 4.3, -1.6, 1.3, 6.6, 'x', 'paper', { cap: 'paper' }),
      box(-6.6, 2.55, -0.4, 5.6, 0.12, 0.5, 'chrome')), ex: [-3, 9, -2], delay: 0.4, inner: true },
    { id: 'power', short: '电源', name: '电源与功率监测', parts: module3('pwr', frame([8.4, 4.6, -1.41], [1, 0, 0], [0, 1, 0], [0, 0, 1]), 44, 26, 'pcbDark', [
      P(18, 13, 10, 10, 3.8, 'inductor'), P(34, 9, 6.6, 6.1, 2.3, 'ic'), P(34, 19, 4.9, 3.9, 1.6, 'ic'), P(6, 13, 5, 10, 6, 'terminal'), P(26, 4, 3, 3, 1, 'ic')]), ex: [8, 7, -2.5], delay: 0.4, inner: true },
    { id: 'fan', short: '风扇', name: '散热风扇', parts: cat(box(-10.6, -1.2, -1.9, 0.7, 4.6, 4.6, 'fanFrame'), cyl(-11.0, -1.2, -1.9, 0.55, 0.3, 'x', 'metal', { n: 14 }), blades(-11.0, -1.2, -1.9, 2.0, 'blade')), ex: [-9, 0, -3], delay: 0.35, inner: true },
    { id: 'panel', short: '前面板', name: '前面板（旋钮/状态灯）', parts: cat(
      // EC11 knob over ENC101, K1 over K101, five status windows over HL101..HL105.
      mount(TOP, cyl(KNOB[0], -H / 2 - 0.05, KNOB[1], 1.2, 0.1, 'y', 'chrome', { n: 32 })),
      mount(TOP, cyl(KEY[0], -H / 2 - 0.03, KEY[1], 0.68, 0.06, 'y', 'seam', { n: 24 })),
      mount(TOP, cat(
        cyl(KNOB[0], -H / 2 - 0.62, KNOB[1], 1.0, 1.15, 'y', 'knob', { n: 36, knurl: 0.06, cap: 'knobTop', back: 'knobTop', backDecal: 'knobMark' }),
        cyl(KEY[0], -H / 2 - 0.14, KEY[1], 0.55, 0.24, 'y', 'button', { n: 22 }),
        ...LEDS.map((c, i) => plate3(c[0], -H / 2 - 0.02, c[1], 0.62, 0.62, 'ledWin').map((f) => Object.assign(f, { led: i })))), 1),
      enclosed(FRONTB ? pcb(PANEL, FRONTB.w, FRONTB.h, 'front', 'pcb', FRONTB.parts) : box(4, -H / 2 + 0.75, 0, 6, 0.16, 4, 'pcb'))), ex: [0, -8, 0], delay: 0.1 },
  ])().map((m) => (m.inner ? Object.assign(m, { parts: m.parts.map((f) => (f.mount ? f : Object.assign({}, f, { inside: true }))) }) : m));

  // ------------------------------------------------------------ materials
  function palette(dark) {
    return {
      shell: dark ? { c: [56, 61, 71], s: 0.4, p: 18 } : { c: [234, 237, 242], s: 0.3, p: 16 },
      trim: { c: rgb(css('--accent')), s: 0.8, p: 30, e: 0.25 },
      glass: { c: [12, 15, 22], s: 0.9, p: 60 },
      glassHi: { c: [18, 22, 30], s: 0.5, p: 40 },
      bezel: { c: [20, 23, 29], s: 0.5, p: 40 },
      screen: { c: [8, 12, 20], s: 0.8, p: 70 },
      metal: { c: [120, 126, 136], s: 0.9, p: 26 },
      chrome: { c: [176, 182, 192], s: 1.1, p: 40 },
      lens: { c: [6, 8, 14], s: 1.0, p: 80 },
      lamp: { c: [214, 218, 224], s: 0.4, p: 20 },
      laser: { c: [60, 20, 26], s: 0.9, p: 50 },
      sensorWin: { c: [14, 16, 22], s: 0.9, p: 60 },
      camWin: { c: [6, 8, 12], s: 1.0, p: 70 },
      lensRing: { c: [28, 30, 36], s: 0.6, p: 30 },
      lampCore: { c: [236, 238, 242], s: 0.3, p: 20 },
      screw: { c: [158, 164, 174], s: 0.9, p: 30 },
      rubber: { c: [38, 40, 44], s: 0.1, p: 8 },
      header: { c: [24, 24, 26], s: 0.3, p: 14 },
      pcb: { c: [24, 84, 66], s: 0.25, p: 12 },
      pcbDark: { c: [28, 34, 44], s: 0.25, p: 12 },
      pcbBlue: { c: [30, 64, 128], s: 0.25, p: 12 },
      pcbTeal: { c: [16, 110, 112], s: 0.25, p: 12 },
      pcbViolet: { c: [86, 52, 122], s: 0.25, p: 12 },
      chip: { c: [26, 28, 32], s: 0.35, p: 20 },
      conn: { c: [232, 228, 218], s: 0.2, p: 10 },
      cap: { c: [70, 74, 82], s: 0.8, p: 30 },
      heatsink: { c: [150, 156, 166], s: 0.7, p: 20 },
      printer: { c: dark ? [70, 76, 86] : [196, 201, 209], s: 0.3, p: 14 },
      paper: { c: [246, 244, 238], s: 0.15, p: 8 },
      inductor: { c: [46, 48, 54], s: 0.4, p: 16 },
      fanFrame: { c: [34, 37, 43], s: 0.3, p: 14 },
      blade: { c: [58, 62, 70], s: 0.4, p: 16 },
      knob: { c: [92, 98, 108], s: 0.9, p: 28 },
      knobTop: { c: [140, 146, 156], s: 1.0, p: 36 },
      button: { c: dark ? [40, 44, 52] : [210, 214, 220], s: 0.5, p: 20 },
      port: { c: [16, 18, 22], s: 0.3, p: 10 },
      grille: { c: dark ? [44, 48, 56] : [216, 220, 226], s: 0.2, p: 10 },
      seam: { c: dark ? [24, 26, 31] : [150, 156, 166], s: 0.15, p: 10 },
      ledWin: { c: [20, 22, 27], s: 0.9, p: 50 },
      elcap: { c: [30, 33, 44], s: 0.5, p: 24 },
      elcapTop: { c: [186, 190, 198], s: 0.9, p: 30 },
      polycap: { c: [34, 32, 30], s: 0.3, p: 16 },
      led: { c: [238, 238, 234], s: 0.4, p: 20 },
      tact: { c: [40, 41, 44], s: 0.5, p: 20 },
      jack: { c: [28, 29, 32], s: 0.35, p: 16 },
      terminal: { c: [38, 132, 86], s: 0.25, p: 14 },
      usb: { c: [192, 196, 204], s: 1.0, p: 34 },
      coin: { c: [30, 31, 35], s: 0.3, p: 14 },
      coinTop: { c: [180, 184, 192], s: 1.0, p: 36 },
      brass: { c: [196, 158, 84], s: 0.9, p: 30 },
      shield: { c: [198, 202, 210], s: 1.0, p: 40 },
    };
  }

  function rot(p, ry, rx) {
    const c = Math.cos(ry), s = Math.sin(ry);
    const x = p[0] * c + p[2] * s;
    let z = -p[0] * s + p[2] * c;
    const c2 = Math.cos(rx), s2 = Math.sin(rx);
    const y = p[1] * c2 - z * s2;
    z = p[1] * s2 + z * c2;
    return [x, y, z];
  }
  const STAG = 0.45;  // spread of the explode start times
  const ease = (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);
  const bil2 = (q, u, v) => {
    const a = [q[0][0] + (q[1][0] - q[0][0]) * u, q[0][1] + (q[1][1] - q[0][1]) * u];
    const b = [q[3][0] + (q[2][0] - q[3][0]) * u, q[3][1] + (q[2][1] - q[3][1]) * u];
    return [a[0] + (b[0] - a[0]) * v, a[1] + (b[1] - a[1]) * v];
  };
  function rr(g, x, y, w, h, r) { g.beginPath(); g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r); g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath(); }
  function inside(x, y, pts) {
    let c = false;
    for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
      if ((pts[i][1] > y) !== (pts[j][1] > y) && x < (pts[j][0] - pts[i][0]) * (y - pts[i][1]) / (pts[j][1] - pts[i][1]) + pts[i][0]) c = !c;
    }
    return c;
  }

  // Textures: the real boards' Gerber renders (loaded once), the modules'
  // procedural layouts (painted once on first use).
  const TEX = {};
  function texFor(key) {
    let t = TEX[key];
    if (t) return t.ready ? t.img : null;
    const real = REAL(key);
    if (real) {
      const img = new Image();
      t = TEX[key] = { img, ready: false };
      img.onload = () => { t.ready = true; Twin.all.forEach((tw) => tw.kick()); };
      img.src = real.tex;
      return null;
    }
    if (!PROC[key]) return null;
    TEX[key] = { img: paintBoard(PROC[key], key), ready: true };
    return TEX[key].img;
  }
  const MASK = { pcb: [[14, 74, 46], [30, 112, 70]], pcbDark: [[16, 18, 22], [34, 38, 46]], pcbBlue: [[18, 44, 104], [34, 70, 150]],
    pcbTeal: [[8, 86, 92], [18, 120, 126]], pcbViolet: [[60, 34, 92], [90, 56, 130]] };
  // A plausible layout from the part list: copper pour under the mask with
  // clearances, 45-degree buses between neighbouring parts, vias, gold pads
  // along each body, silkscreen outlines with pin-1 marks, mounting holes.
  function paintBoard(spec, key) {
    const k = Math.min(10, 1100 / Math.max(spec.w, spec.h));
    const cv = document.createElement('canvas');
    cv.width = Math.round(spec.w * k); cv.height = Math.round(spec.h * k);
    const g = cv.getContext('2d'), col = MASK[spec.mat] || MASK.pcb;
    let seed = 0; for (let i = 0; i < key.length; i++) seed = (seed * 31 + key.charCodeAt(i)) >>> 0;
    const rnd = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 4294967296);
    const c = (a, al) => 'rgba(' + a.join(',') + ',' + (al == null ? 1 : al) + ')';
    g.scale(k, k);
    g.fillStyle = c(col[0]); g.fillRect(0, 0, spec.w, spec.h);
    g.fillStyle = c(col[1]); g.fillRect(1.2, 1.2, spec.w - 2.4, spec.h - 2.4);  // ground pour
    const parts = spec.parts.map((p) => { const sw = Math.abs(Math.sin((p.a || 0) * Math.PI / 180)) > 0.7; return { x: p.x, y: p.y, l: sw ? p.w : p.l, w: sw ? p.l : p.w, s: p.s }; });
    // Traces: drawn as a clearance (mask colour) and the copper on top.
    const route = (pts, n, gap) => {
      for (let i = 0; i < n; i++) {
        const o = (i - (n - 1) / 2) * gap;
        const q = pts.map((p) => [p[0] + (p[2] || 0) * o, p[1] + (p[3] || 0) * o]);
        [[c(col[0]), 0.62], [c(col[1].map((v) => Math.min(255, v + 18))), 0.26]].forEach((st) => {
          g.strokeStyle = st[0]; g.lineWidth = st[1]; g.lineJoin = 'round'; g.lineCap = 'round';
          g.beginPath(); q.forEach((p, j) => (j ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]))); g.stroke();
        });
      }
    };
    const sorted = parts.slice().sort((a, b) => a.x - b.x);
    for (let i = 0; i + 1 < sorted.length; i++) {
      const a = sorted[i], b = sorted[i + 1];
      const x0 = a.x + a.l / 2 + 0.6, x1 = b.x - b.l / 2 - 0.6;
      if (x1 - x0 < 2) continue;
      const dy = b.y - a.y, run = Math.min(Math.abs(dy), (x1 - x0) * 0.6), mid = x0 + (x1 - x0 - run) / 2;
      const ya = a.y + (rnd() - 0.5) * a.w * 0.3, yb = ya + Math.sign(dy) * run;
      const n = 2 + Math.floor(rnd() * 4);
      // Parallel traces are offset in y; the 45-degree leg stays parallel.
      route([[x0, ya, 0, 1], [mid, ya, 0, 1], [mid + run, yb, 0, 1], [x1, yb, 0, 1]], n, 0.65);
    }
    // Stubs to vias.
    const vias = [];
    parts.forEach((p) => {
      const m = 1 + Math.floor(rnd() * 3);
      for (let i = 0; i < m; i++) {
        const side = rnd() > 0.5 ? 1 : -1, horiz = rnd() > 0.5;
        const sx = horiz ? p.x + side * (p.l / 2 + 0.5) : p.x + (rnd() - 0.5) * p.l * 0.8;
        const sy = horiz ? p.y + (rnd() - 0.5) * p.w * 0.8 : p.y + side * (p.w / 2 + 0.5);
        const len = 2 + rnd() * 5, ex = horiz ? sx + side * len : sx + (rnd() - 0.5) * len, ey = horiz ? sy + (rnd() - 0.5) * len : sy + side * len;
        if (ex < 2 || ey < 2 || ex > spec.w - 2 || ey > spec.h - 2) continue;
        const kx = horiz ? sx + side * len * 0.5 : sx, ky = horiz ? sy : sy + side * len * 0.5;
        route([[sx, sy], [kx, ky], [ex, ey]], 1, 0);
        vias.push([ex, ey]);
      }
    });
    vias.forEach((v) => {
      g.fillStyle = c(col[1].map((x) => Math.min(255, x + 30))); g.beginPath(); g.arc(v[0], v[1], 0.42, 0, Math.PI * 2); g.fill();
      g.fillStyle = 'rgb(12,14,16)'; g.beginPath(); g.arc(v[0], v[1], 0.18, 0, Math.PI * 2); g.fill();
    });
    // Pads along the long sides, then the silkscreen outline and pin-1 mark.
    parts.forEach((p) => {
      g.fillStyle = 'rgb(214,176,98)';
      if (p.s === 'cyl') { [[-1, 0], [1, 0]].forEach((d) => g.fillRect(p.x + d[0] * p.l * 0.32 - 0.8, p.y - 0.6, 1.6, 1.2)); }
      else {
        const along = p.l >= p.w, len = along ? p.l : p.w, n = Math.max(2, Math.min(28, Math.round(len / 1.27)));
        for (let i = 0; i < n; i++) {
          const t = -len / 2 + len * (i + 0.5) / n;
          [-1, 1].forEach((sd) => {
            if (along) g.fillRect(p.x + t - 0.3, p.y + sd * (p.w / 2 + 0.25) - 0.45, 0.6, 0.9);
            else g.fillRect(p.x + sd * (p.l / 2 + 0.25) - 0.45, p.y + t - 0.3, 0.9, 0.6);
          });
        }
      }
      g.strokeStyle = 'rgba(236,238,234,.9)'; g.lineWidth = 0.18;
      g.strokeRect(p.x - p.l / 2 - 0.8, p.y - p.w / 2 - 0.8, p.l + 1.6, p.w + 1.6);
      g.fillStyle = 'rgba(236,238,234,.9)'; g.beginPath(); g.arc(p.x - p.l / 2 - 1.3, p.y - p.w / 2 - 1.3, 0.3, 0, Math.PI * 2); g.fill();
      g.fillRect(p.x - p.l / 2, p.y + p.w / 2 + 1.3, Math.min(4, p.l * 0.5), 0.5);  // reference designator
    });
    if (spec.w > 25 && spec.h > 25) [[3.5, 3.5], [spec.w - 3.5, 3.5], [3.5, spec.h - 3.5], [spec.w - 3.5, spec.h - 3.5]].forEach((h) => {
      g.fillStyle = 'rgb(214,176,98)'; g.beginPath(); g.arc(h[0], h[1], 2.2, 0, Math.PI * 2); g.fill();
      g.fillStyle = 'rgb(14,15,17)'; g.beginPath(); g.arc(h[0], h[1], 1.4, 0, Math.PI * 2); g.fill();
    });
    return cv;
  }

  // One textured triangle: screen points s0..s2, texture points t0..t2. The
  // clip is grown by half a pixel so neighbouring tiles leave no hairline.
  function tri(g, img, s0, s1, s2, t0, t1, t2) {
    const cx = (s0[0] + s1[0] + s2[0]) / 3, cy = (s0[1] + s1[1] + s2[1]) / 3;
    const grow = (p) => { const dx = p[0] - cx, dy = p[1] - cy, l = Math.hypot(dx, dy) || 1; return [p[0] + dx / l * 0.6, p[1] + dy / l * 0.6]; };
    const d = (t1[0] - t0[0]) * (t2[1] - t0[1]) - (t2[0] - t0[0]) * (t1[1] - t0[1]);
    if (Math.abs(d) < 1e-6) return;
    const a = ((s1[0] - s0[0]) * (t2[1] - t0[1]) - (s2[0] - s0[0]) * (t1[1] - t0[1])) / d;
    const b = ((s1[1] - s0[1]) * (t2[1] - t0[1]) - (s2[1] - s0[1]) * (t1[1] - t0[1])) / d;
    const c = ((s2[0] - s0[0]) * (t1[0] - t0[0]) - (s1[0] - s0[0]) * (t2[0] - t0[0])) / d;
    const e = ((s2[1] - s0[1]) * (t1[0] - t0[0]) - (s1[1] - s0[1]) * (t2[0] - t0[0])) / d;
    g.save();
    const p0 = grow(s0), p1 = grow(s1), p2 = grow(s2);
    g.beginPath(); g.moveTo(p0[0], p0[1]); g.lineTo(p1[0], p1[1]); g.lineTo(p2[0], p2[1]); g.closePath(); g.clip();
    g.transform(a, b, c, e, s0[0] - a * t0[0] - c * t0[1], s0[1] - b * t0[0] - e * t0[1]);
    g.drawImage(img, 0, 0);
    g.restore();
  }

  class Twin {
    constructor(canvas, opts) {
      this.c = canvas;
      this.o = opts || {};
      this.ry = -0.55; this.rx = -0.32; this.zoom = 1; this.vel = 0;
      this.explodeT = 0; this.explodeTo = 0;
      this.sel = null; this.status = {}; this.lightOn = false; this.laserOn = false; this.fanOn = false; this.ledLevel = 'unknown';
      this.auto = true; this.running = false; this.hits = []; this.spin = 0; this.wave = null;
      this.bind();
      Twin.all.add(this);
    }
    setStatus(map) { this.status = map || {}; this.kick(); }
    setOutputs(light, laser, level, fan, wave) { this.lightOn = light; this.laserOn = laser; this.ledLevel = level; this.fanOn = fan; if (wave) this.wave = wave; this.kick(); }
    setExplode(v) { this.explodeTo = U.clamp(v, 0, 1); this.kick(); if (this.o.onExplode) this.o.onExplode(this.explodeTo); }
    explode(on) { this.setExplode(on ? 1 : 0); }
    select(id) { this.sel = id; if (id && this.explodeTo < 0.5) this.setExplode(1); this.kick(); if (this.o.onSelect) this.o.onSelect(id); }
    reset() { this.ry = -0.55; this.rx = -0.32; this.zoom = this.o.zoomRange ? U.clamp(1, this.o.zoomRange[0], this.o.zoomRange[1]) : 1; this.auto = true; this.kick(); }
    modules() { return MODULES.filter((m) => !m.shell); }
    bind() {
      const c = this.c;
      let last = null, pinch = null, moved = 0, t0 = 0, lastTouch = 0;
      // A tap fires touch events and then emulated mouse events; handling both
      // selected and immediately deselected the part.
      const fromMouse = (e) => !e.touches && !e.changedTouches;
      const stale = (e) => fromMouse(e) && Date.now() - lastTouch < 900;
      const pos = (e) => (e.touches ? [e.touches[0].clientX, e.touches[0].clientY] : [e.clientX, e.clientY]);
      const rxr = this.o.rxRange || [-1.3, 1.3], zr = this.o.zoomRange || [0.6, 2.6];
      const start = (e) => {
        if (e.touches) lastTouch = Date.now();
        else if (stale(e)) return;
        if (e.touches && e.touches.length === 2) { pinch = Math.hypot(e.touches[0].clientX - e.touches[1].clientX, e.touches[0].clientY - e.touches[1].clientY); last = null; return; }
        last = pos(e); moved = 0; t0 = Date.now(); this.auto = false; this.vel = 0; this.resumeAt = 0;
      };
      const move = (e) => {
        if (e.touches && e.touches.length === 2 && pinch) {
          const d = Math.hypot(e.touches[0].clientX - e.touches[1].clientX, e.touches[0].clientY - e.touches[1].clientY);
          this.zoom = U.clamp(this.zoom * d / pinch, zr[0], zr[1]); pinch = d; this.kick();
          if (e.cancelable) e.preventDefault();
          return;
        }
        if (!last) return;
        const p = pos(e);
        const dx = p[0] - last[0], dy = p[1] - last[1];
        moved += Math.abs(dx) + Math.abs(dy);
        this.vel = dx * 0.011;
        this.ry += dx * 0.011; this.rx = U.clamp(this.rx - dy * 0.008, rxr[0], rxr[1]);
        last = p; this.kick();
        if (e.cancelable) e.preventDefault();
      };
      const end = (e) => {
        if (e.changedTouches) { lastTouch = Date.now(); if (e.cancelable) e.preventDefault(); }  // no emulated click / dblclick
        if (pinch && (!e.touches || e.touches.length < 2)) { pinch = null; last = null; return; }
        if (last && moved < 8 && Date.now() - t0 < 400) this.tap(last[0], last[1]);
        if (this.o.resumeMs) this.resumeAt = Date.now() + this.o.resumeMs;
        last = null; this.kick();
      };
      c.addEventListener('touchstart', start, { passive: true });
      c.addEventListener('touchmove', move, { passive: false });
      c.addEventListener('touchend', end, { passive: false });
      c.addEventListener('touchcancel', () => { last = null; pinch = null; if (this.o.resumeMs) this.resumeAt = Date.now() + this.o.resumeMs; });
      c.addEventListener('mousedown', start);
      window.addEventListener('mousemove', (e) => { if (last) move(e); });
      window.addEventListener('mouseup', (e) => { if (last) end(e); });
      c.addEventListener('wheel', (e) => { this.zoom = U.clamp(this.zoom * (e.deltaY < 0 ? 1.08 : 0.93), zr[0], zr[1]); this.kick(); e.preventDefault(); }, { passive: false });
      c.addEventListener('dblclick', (e) => { if (!stale(e)) this.reset(); });
      if (typeof IntersectionObserver !== 'undefined') new IntersectionObserver((en) => { this.visible = en[0].isIntersecting; if (this.visible) this.kick(); }).observe(c);
    }
    tap(x, y) {
      if (this.o.tap === false) return;
      const r = this.c.getBoundingClientRect();
      const px = x - r.left, py = y - r.top;
      for (let i = this.hits.length - 1; i >= 0; i--) {
        const h = this.hits[i];
        if (inside(px, py, h.pts)) {
          U.haptic(8);
          if (h.shell) { if (this.explodeTo < 0.5) this.setExplode(1); return; }  // the closed device opens
          this.select(this.sel === h.id ? null : h.id);
          return;
        }
      }
      if (this.sel) this.select(null);
    }
    kick() {
      if (this.running) return;
      this.running = true;
      const step = () => {
        if (!this.c.isConnected || document.hidden || this.visible === false || !this.c.offsetParent) { this.running = false; return; }
        this.draw();
        requestAnimationFrame(step);
      };
      requestAnimationFrame(step);
    }
    draw() {
      const c = this.c;
      const dpr = Math.min(2.5, window.devicePixelRatio || 1);
      const Wc = c.clientWidth, Hc = c.clientHeight;
      if (!Wc || !Hc) return;
      if (c.width !== Math.round(Wc * dpr) || c.height !== Math.round(Hc * dpr)) { c.width = Math.round(Wc * dpr); c.height = Math.round(Hc * dpr); }
      const g = c.getContext('2d');
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, Wc, Hc);
      const t = performance.now() / 1000;
      // Spin again once the drag's coast has died down.
      if (this.resumeAt && Date.now() > this.resumeAt && Math.abs(this.vel) < 0.002) { this.auto = true; this.resumeAt = 0; }
      if (this.auto && this.explodeTo > 0.3) {
        // Exploded: settle into a three-quarter view that shows the layers.
        const target = -0.95 + Math.round((this.ry + 0.95) / (Math.PI * 2)) * Math.PI * 2;
        this.ry += (target - this.ry) * 0.05;
        this.rx += (-0.42 - this.rx) * 0.05;
      } else if (this.auto) {
        this.ry += 0.0028;
        if (this.o.resumeMs) this.rx += (-0.32 - this.rx) * 0.03;  // ease back to the resting tilt
      }
      else if (Math.abs(this.vel) > 0.0004) { this.ry += this.vel; this.vel *= 0.93; }
      this.explodeT += (this.explodeTo - this.explodeT) * 0.08;
      const e = ease(U.clamp(this.explodeT, 0, 1));
      this.spin += this.fanOn ? 0.35 : 0.015;
      const dark = document.documentElement.getAttribute('data-theme') === 'dark';
      const M = palette(dark);
      const scale = Math.min(Wc / (29 + 16 * e), Hc / (22 + 24 * e)) * this.zoom * (this.fit || 1);
      const cx = Wc / 2, cy = Hc * 0.5, F = 140;
      const L = norm([-0.45, -0.75, 0.55]), L2 = norm([0.8, 0.1, -0.4]);
      const proj = (q) => { const s = F / (F - q[2]); return [cx + q[0] * s * scale, cy + q[1] * s * scale, q[2]]; };
      const accent = rgb(css('--accent'));
      const stCol = { ok: rgb(css('--ok')), warn: rgb(css('--warn')), alarm: rgb(css('--alarm')), off: rgb(css('--text-3')), unknown: rgb(css('--unknown')) };
      // Soft floor shadow under the (spread) device.
      const floorY = H / 2 + 1.2 + e * 9;
      const sh = [[-W / 2 - 1.5, floorY, ZB - 1.5 - 10 * e], [W / 2 + 1.5, floorY, ZB - 1.5 - 10 * e], [W / 2 + 1.5, floorY, ZF + 1.5 + 8 * e], [-W / 2 - 1.5, floorY, ZF + 1.5 + 8 * e]].map((p) => proj(rot(p, this.ry, this.rx)));
      g.save();
      g.filter = 'blur(18px)';
      g.fillStyle = dark ? 'rgba(0,0,0,.6)' : 'rgba(30,40,80,.16)';
      g.beginPath(); sh.forEach((p, i) => (i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]))); g.closePath(); g.fill();
      g.restore();
      const polys = [], centers = {};
      const spinC = Math.cos(this.spin), spinS = Math.sin(this.spin);
      MODULES.forEach((m) => {
        // Each module leaves on its own beat: shells first, then the layers inside.
        const raw = U.clamp(this.explodeT, 0, 1);
        const em = ease(U.clamp((raw * (1 + STAG) - (m.delay || 0)) / 1, 0, 1));
        const attached = em < 0.5;  // still on (or close to) the housing
        const off = m.ex.map((v) => v * em);
        const shellAlpha = m.shell ? 1 - 0.9 * U.clamp(em * 1.3, 0, 1) : 1;
        let sx = 0, sy = 0, sz = 0, cnt = 0;
        m.parts.forEach((f) => {
          const w3 = f.decal ? [] : null;  // view-space corners, for decals in perspective
          const pts = f.p.map((p0) => {
            let p = p0;
            if (f.spin) { const dy = p[1] - f.spin[0], dz = p[2] - f.spin[1]; p = [p[0], f.spin[0] + dy * spinC - dz * spinS, f.spin[1] + dy * spinS + dz * spinC]; }
            const q = rot([p[0] + off[0], p[1] + off[1], p[2] + off[2]], this.ry, this.rx);
            sx += q[0]; sy += q[1]; sz += q[2]; cnt++;
            if (w3) w3.push(q);
            return proj(q);
          });
          if (f.inside && em < 0.03) return;  // hidden inside the closed housing
          const n = rot(f.n, this.ry, this.rx);
          if (n[2] <= 0.0001 && !f.spin) return;
          const onTop = f.mount && attached;
          if (onTop && rot(f.mount, this.ry, this.rx)[2] < 0.02) return;  // its surface faces away
          let z = 0; pts.forEach((p) => { z += p[2]; });
          polys.push({ m, f, pts, n, z: z / pts.length, alpha: shellAlpha, onTop, w3 });
        });
        if (cnt) centers[m.id] = proj([sx / cnt, sy / cnt, sz / cnt]);
      });
      if (this.o.autoFit) {
        // Keep the whole outline on the canvas at any tilt: shrink (smoothly)
        // only when it would cross an edge, and grow back when it no longer does.
        let need = 0;
        polys.forEach((pp) => pp.pts.forEach((q) => { need = Math.max(need, Math.abs(q[0] - cx) / (Wc / 2 - 8), Math.abs(q[1] - cy) / (Hc / 2 - 12)); }));
        const f0 = this.fit || 1;
        if (need > 0) this.fit = f0 + (Math.min(1, f0 / need * 0.97) - f0) * 0.18;
      }
      // Painter's order. Closed: faces by depth (only the housing and what is
      // on it remain). Exploded: each module is a layer, ordered by its mean
      // depth, so a large housing face can no longer cut through a small part.
      // Parts mounted on the housing go last, over the surface they sit on.
      const layered = e > 0.15;
      const depth = {};
      if (layered) {
        const acc = {};
        polys.forEach((p) => { if (p.onTop) return; const a = acc[p.m.id] || (acc[p.m.id] = [0, 0]); a[0] += p.z; a[1]++; });
        Object.keys(acc).forEach((k) => { depth[k] = acc[k][0] / acc[k][1]; });
      }
      polys.sort((a, b) => {
        if (a.onTop !== b.onTop) return a.onTop ? 1 : -1;
        if (layered && !a.onTop && a.m !== b.m) return depth[a.m.id] - depth[b.m.id];
        if (a.onTop && (a.f.layer || 0) !== (b.f.layer || 0)) return (a.f.layer || 0) - (b.f.layer || 0);
        return a.z - b.z;
      });
      this.hits = [];
      const V = [0, 0, 1];
      const hv = norm([L[0] + V[0], L[1] + V[1], L[2] + V[2]]);
      polys.forEach((p) => {
        const m = p.m, f = p.f, n = p.n;
        const mat = M[f.mat] || M.shell;
        const lam = Math.max(0, n[0] * L[0] + n[1] * L[1] + n[2] * L[2]);
        const rim = Math.max(0, n[0] * L2[0] + n[1] * L2[1] + n[2] * L2[2]) * 0.18;
        const spec = Math.pow(Math.max(0, n[0] * hv[0] + n[1] * hv[1] + n[2] * hv[2]), mat.p) * mat.s;
        let col = mat.c.map((v) => v * (0.5 + 0.55 * lam + rim) + 255 * spec * 0.55);
        if (mat.e) col = col.map((v, i) => v + mat.c[i] * mat.e);
        if (m.glow === 'light' && this.lightOn && (f.mat === 'lamp' || f.mat === 'lampCore') && n[2] > 0.4) col = f.mat === 'lampCore' ? [255, 255, 255] : [255, 246, 226];
        if (f.mat === 'laser' && this.laserOn) col = [255, 70, 80];
        if (f.mat === 'ledWin') { const lc = this.panelLed(f.led, t); if (lc) col = lc; }
        let alpha = p.alpha;
        if (this.sel && this.sel !== m.id) alpha *= m.shell ? 0.5 : 0.2;
        if (this.sel === m.id) col = col.map((v, i) => v * 0.72 + accent[i] * 0.28);
        col = col.map((v) => Math.max(0, Math.min(255, Math.round(v))));
        g.globalAlpha = alpha;
        g.beginPath();
        p.pts.forEach((q, i) => (i ? g.lineTo(q[0], q[1]) : g.moveTo(q[0], q[1])));
        g.closePath();
        const fill = 'rgb(' + col.join(',') + ')';
        g.fillStyle = fill; g.fill();
        g.strokeStyle = fill; g.lineWidth = 0.6; g.stroke();  // closes hairline seams between faces
        if (f.decal) this.decal(g, f.decal, p.pts, t, dark, p.w3, proj);
        if (this.sel === m.id) { g.strokeStyle = 'rgba(' + accent.join(',') + ',.9)'; g.lineWidth = 1; g.stroke(); }
        g.globalAlpha = 1;
        if (!m.shell || e < 0.5) this.hits.push({ id: m.id, pts: p.pts, shell: m.shell, onTop: p.onTop });
      });
      this.glows(g, proj, e, dark);
      this.centers = centers;
    }
    glows(g, proj, e, dark) {
      const front = rot([0, 0, 1], this.ry, this.rx)[2];
      if (front <= 0.05 || !(this.lightOn || this.laserOn)) return;
      g.save(); g.globalCompositeOperation = dark ? 'lighter' : 'source-over';
      const at = (x) => proj(rot([x, -5.2, ZF + 0.2 + 15 * ease(U.clamp(this.explodeT * (1 + STAG), 0, 1))], this.ry, this.rx));
      if (this.lightOn) [-7.2, 7.2].forEach((x) => {
        const q = at(x), r = 46 * this.zoom;
        const gr = g.createRadialGradient(q[0], q[1], 0, q[0], q[1], r);
        gr.addColorStop(0, 'rgba(255,248,225,' + (dark ? 0.35 : 0.5) + ')'); gr.addColorStop(1, 'rgba(255,248,225,0)');
        g.fillStyle = gr; g.beginPath(); g.arc(q[0], q[1], r, 0, Math.PI * 2); g.fill();
      });
      if (this.laserOn) {
        const q = at(0);
        g.strokeStyle = 'rgba(255,60,72,.65)'; g.lineWidth = 1.2;
        g.beginPath(); g.moveTo(q[0] - 30, q[1]); g.lineTo(q[0] + 30, q[1]); g.moveTo(q[0], q[1] - 30); g.lineTo(q[0], q[1] + 30); g.stroke();
      }
      g.restore();
    }
    setLive(l) { this.live = l || null; }
    // HL101..HL105 as panel_service.cpp drives them: system, network,
    // measurement, peripherals, alarm level. Without a device: idle lights.
    panelLed(i, t) {
      const l = this.live, ms = t * 1000, blink = (per, on) => ms % per < on;
      const G = [40, 220, 130], Y = [255, 186, 40], R = [255, 70, 80], B = [70, 130, 255];
      const lit = (c, k) => c.map((v) => v * (k == null ? 1 : k));
      if (!l) return [lit(G, blink(2400, 1400) ? 1 : 0.25), lit(G, 0.9), null, null, lit(G, 0.45)][i];
      const nano = l.nano || {};
      switch (i) {
        case 0: return l.cam === false ? lit(Y) : blink(2000, 300) ? lit(G) : null;
        case 1: return !nano.on ? null : nano.wifi === 'CONNECTED' ? lit(G) : nano.wifi === 'CONNECTING' ? (blink(1000, 500) ? lit(B) : null) : nano.wifi === 'FAILED' ? lit(Y) : null;
        case 2: return l.ms === 'capturing' ? (blink(500, 250) ? lit(G) : null) : l.ms === 'analyzing' ? lit(B) : null;
        case 3: return l.prn === 'printing' ? (blink(500, 250) ? lit(Y) : null) : null;
        default: return l.lv === 'alarm' ? lit(R) : l.lv === 'warn' ? lit(Y) : l.lv === 'normal' ? lit(G, 0.45) : null;
      }
    }
    ledCol(t) {
      const lv = this.ledLevel;
      const base = lv === 'alarm' ? [255, 77, 90] : lv === 'warn' ? [255, 176, 32] : lv === 'normal' ? [40, 210, 150] : rgb(css('--accent'));
      const k = 0.75 + 0.25 * Math.sin(t * (lv === 'alarm' ? 8 : 2.2));
      return base.map((v) => v * k);
    }
    decal(g, kind, q, t, dark, w3, proj) {
      const spec = typeof kind === 'object' ? kind : null;
      if (spec) kind = spec.k;
      // (u, v) on a quad face: interpolated on the face in 3D, then projected,
      // so decals follow the perspective exactly (a bilinear blend of the
      // projected corners drifts at oblique views).
      const bil = (qq, u, v) => {
        if (qq !== q || !w3 || w3.length !== 4) return bil2(qq, u, v);
        const a = w3[0], b = w3[1], c = w3[2], d = w3[3], P = [0, 0, 0];
        for (let i = 0; i < 3; i++) P[i] = (a[i] + (b[i] - a[i]) * u) * (1 - v) + (d[i] + (c[i] - d[i]) * u) * v;
        return proj(P);
      };
      g.save();
      g.beginPath(); q.forEach((p, i) => (i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]))); g.closePath(); g.clip();
      if (kind === 'sheen') {
        let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        q.forEach((p) => { x0 = Math.min(x0, p[0]); y0 = Math.min(y0, p[1]); x1 = Math.max(x1, p[0]); y1 = Math.max(y1, p[1]); });
        const gr = g.createLinearGradient(x0, y0, x1, y1);
        gr.addColorStop(0, 'rgba(255,255,255,.02)'); gr.addColorStop(0.32, 'rgba(255,255,255,.13)'); gr.addColorStop(0.36, 'rgba(255,255,255,.04)');
        gr.addColorStop(0.62, 'rgba(120,140,255,.05)'); gr.addColorStop(1, 'rgba(255,255,255,0)');
        g.fillStyle = gr; g.fill();
      } else if (kind === 'screen') {
        // A small live UI drawn in the screen's own (u, v) plane, so it stays
        // on the glass whatever the view angle.
        const acc = rgb(css('--accent'));
        const poly = (uv) => { g.beginPath(); uv.forEach((p, i) => { const s2 = bil(q, p[0], p[1]); if (i) g.lineTo(s2[0], s2[1]); else g.moveTo(s2[0], s2[1]); }); g.closePath(); };
        const quad = (u0, v0, u1, v1) => poly([[u0, v0], [u1, v0], [u1, v1], [u0, v1]]);
        const a = bil(q, 0, 0), b = bil(q, 1, 1);
        const gr = g.createLinearGradient(a[0], a[1], b[0], b[1]);
        gr.addColorStop(0, 'rgba(' + acc.join(',') + ',.30)'); gr.addColorStop(1, 'rgba(10,14,24,0)');
        g.fillStyle = gr; g.fill();
        // Status line and page tabs.
        g.fillStyle = 'rgba(255,255,255,.55)';
        quad(0.06, 0.07, 0.2, 0.1); g.fill();
        quad(0.82, 0.07, 0.94, 0.1); g.fill();
        g.fillStyle = 'rgba(255,255,255,.18)';
        for (let i = 0; i < 6; i++) { quad(0.06 + i * 0.148, 0.88, 0.18 + i * 0.148, 0.93); g.fill(); }
        g.fillStyle = 'rgba(' + acc.join(',') + ',.9)';
        quad(0.06, 0.88, 0.18, 0.93); g.fill();
        // Reading blocks and the level ring.
        g.fillStyle = 'rgba(255,255,255,.85)';
        quad(0.06, 0.18, 0.4, 0.3); g.fill();
        g.fillStyle = 'rgba(255,255,255,.35)';
        quad(0.06, 0.33, 0.26, 0.37); g.fill();
        const lc = this.ledCol(t).map(Math.round).join(',');
        g.strokeStyle = 'rgb(' + lc + ')'; g.lineWidth = 2;
        g.beginPath();
        for (let i = 0; i <= 40; i++) {
          const th = -Math.PI * 0.8 + i / 40 * Math.PI * 1.25;
          const p = bil(q, 0.8 + Math.cos(th) * 0.085, 0.28 + Math.sin(th) * 0.145);
          if (i) g.lineTo(p[0], p[1]); else g.moveTo(p[0], p[1]);
        }
        g.stroke();
        // Live waveform.
        g.strokeStyle = 'rgba(' + acc.join(',') + ',.95)'; g.lineWidth = 1.3;
        g.beginPath();
        for (let i = 0; i <= 60; i++) {
          const u = 0.06 + i / 60 * 0.88;
          const w = this.wave && this.wave.length ? this.wave[Math.floor(i / 60 * (this.wave.length - 1))] : Math.sin(i * 0.7 + t * 6);
          const p = bil(q, u, 0.64 + 0.12 * U.clamp(w, -1, 1));
          if (i) g.lineTo(p[0], p[1]); else g.moveTo(p[0], p[1]);
        }
        g.stroke();
        g.fillStyle = 'rgba(255,255,255,.05)';
        poly([[0.55, 0], [0.95, 0], [0.6, 1], [0.2, 1]]); g.fill();  // glass reflection
      } else if (kind === 'screw' || kind === 'capTop') {
        let x = 0, y = 0; q.forEach((p) => { x += p[0]; y += p[1]; });
        const c0 = [x / q.length, y / q.length], k = q.length;
        g.strokeStyle = kind === 'screw' ? 'rgba(30,34,40,.75)' : 'rgba(200,204,212,.5)'; g.lineWidth = 1;
        [[0, k / 2], [k / 4, 3 * k / 4]].forEach((ij) => {
          const p1 = q[Math.round(ij[0]) % k], p2 = q[Math.round(ij[1]) % k];
          g.beginPath(); g.moveTo(c0[0] + (p1[0] - c0[0]) * 0.55, c0[1] + (p1[1] - c0[1]) * 0.55); g.lineTo(c0[0] + (p2[0] - c0[0]) * 0.55, c0[1] + (p2[1] - c0[1]) * 0.55); g.stroke();
        });
      } else if (kind === 'jack') {
        let x = 0, y = 0; q.forEach((p) => { x += p[0]; y += p[1]; });
        const c0 = [x / q.length, y / q.length], sc = (f) => q.map((p) => [c0[0] + (p[0] - c0[0]) * f, c0[1] + (p[1] - c0[1]) * f]);
        g.fillStyle = 'rgba(6,7,9,.95)'; g.beginPath(); sc(0.72).forEach((p, i) => (i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]))); g.closePath(); g.fill();
        g.fillStyle = 'rgba(210,214,222,.9)'; g.beginPath(); sc(0.2).forEach((p, i) => (i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]))); g.closePath(); g.fill();
      } else if (kind === 'knobMark') {
        let x = 0, y = 0; q.forEach((p) => { x += p[0]; y += p[1]; });
        const c0 = [x / q.length, y / q.length], r0 = q[0];
        g.strokeStyle = 'rgba(255,255,255,.85)'; g.lineWidth = 1.6; g.lineCap = 'round';
        g.beginPath(); g.moveTo(c0[0] + (r0[0] - c0[0]) * 0.35, c0[1] + (r0[1] - c0[1]) * 0.35); g.lineTo(c0[0] + (r0[0] - c0[0]) * 0.8, c0[1] + (r0[1] - c0[1]) * 0.8); g.stroke();
      } else if (kind === 'tof') {
        let x = 0, y = 0; q.forEach((p) => { x += p[0]; y += p[1]; });
        const c0 = [x / q.length, y / q.length], ra = q[0], rb = q[Math.floor(q.length / 2)];
        [[ra, '120,40,60'], [rb, '60,70,90']].forEach((d) => {
          const p = [c0[0] + (d[0][0] - c0[0]) * 0.45, c0[1] + (d[0][1] - c0[1]) * 0.45];
          g.fillStyle = 'rgba(' + d[1] + ',.95)'; g.beginPath(); g.arc(p[0], p[1], 1.6, 0, Math.PI * 2); g.fill();
          g.fillStyle = 'rgba(255,255,255,.45)'; g.beginPath(); g.arc(p[0] - 0.5, p[1] - 0.5, 0.5, 0, Math.PI * 2); g.fill();
        });
      } else if (kind === 'header') {
        g.fillStyle = 'rgba(214,178,94,.85)';
        const long = Math.hypot(q[1][0] - q[0][0], q[1][1] - q[0][1]) >= Math.hypot(q[3][0] - q[0][0], q[3][1] - q[0][1]);
        for (let i = 0; i < 10; i++) {
          const tpos = 0.07 + i * 0.096;
          [0.3, 0.7].forEach((o) => { const p = long ? bil(q, tpos, o) : bil(q, o, tpos); g.beginPath(); g.arc(p[0], p[1], 0.8, 0, Math.PI * 2); g.fill(); });
        }
      } else if (kind === 'coil') {
        // PN532 antenna: a few rounded turns.
        g.strokeStyle = 'rgba(214,178,94,.7)'; g.lineWidth = 0.8;
        for (let k = 1; k <= 5; k++) {
          const s = k * 0.06;
          const pts = [bil(q, s, s), bil(q, 1 - s, s), bil(q, 1 - s, 1 - s), bil(q, s, 1 - s)];
          g.beginPath(); pts.forEach((p, i) => (i ? g.lineTo(p[0], p[1]) : g.moveTo(p[0], p[1]))); g.closePath(); g.stroke();
        }
      } else if (kind === 'lens') {
        let x = 0, y = 0; q.forEach((p) => { x += p[0]; y += p[1]; });
        const ctr = [x / q.length, y / q.length];
        let rad = 0; q.forEach((p) => { rad = Math.max(rad, Math.hypot(p[0] - ctr[0], p[1] - ctr[1])); });
        const gr = g.createRadialGradient(ctr[0] - rad * 0.3, ctr[1] - rad * 0.3, rad * 0.05, ctr[0], ctr[1], rad);
        gr.addColorStop(0, 'rgba(96,116,200,.6)'); gr.addColorStop(0.45, 'rgba(34,26,78,.45)'); gr.addColorStop(1, 'rgba(0,0,0,.6)');
        g.fillStyle = gr; g.fill();
        g.strokeStyle = 'rgba(160,170,200,.25)'; g.lineWidth = 1;
        g.beginPath(); g.arc(ctr[0], ctr[1], rad * 0.55, 0, Math.PI * 2); g.stroke();
        g.fillStyle = 'rgba(255,255,255,.85)'; g.beginPath(); g.ellipse(ctr[0] - rad * 0.35, ctr[1] - rad * 0.38, rad * 0.16, rad * 0.09, -0.6, 0, Math.PI * 2); g.fill();
        g.fillStyle = 'rgba(255,255,255,.35)'; g.beginPath(); g.arc(ctr[0] + rad * 0.3, ctr[1] + rad * 0.32, rad * 0.06, 0, Math.PI * 2); g.fill();
      } else if (kind === 'grille') {
        g.strokeStyle = dark ? 'rgba(0,0,0,.6)' : 'rgba(40,50,70,.3)'; g.lineWidth = 1.6; g.lineCap = 'round';
        for (let i = 0; i < 9; i++) { const v = 0.1 + i * 0.1; const a = bil(q, 0.15, v), b = bil(q, 0.85, v); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke(); }
      } else if (kind === 'portC' || kind === 'portDC') {
        // The opening is traced in (u, v) like the screen, not as a screen ellipse.
        g.fillStyle = 'rgba(0,0,0,.7)';
        g.beginPath();
        for (let i = 0; i < 24; i++) {
          const th = i / 24 * Math.PI * 2, p = bil(q, 0.5 + Math.cos(th) * 0.35, 0.5 + Math.sin(th) * 0.2);
          if (i) g.lineTo(p[0], p[1]); else g.moveTo(p[0], p[1]);
        }
        g.closePath(); g.fill();
      } else if (kind === 'micHole') {
        g.fillStyle = 'rgba(0,0,0,.8)';
        for (let i = 0; i < 5; i++) { const p = bil(q, 0.15 + i * 0.175, 0.5); g.beginPath(); g.arc(p[0], p[1], 0.9, 0, Math.PI * 2); g.fill(); }
      } else if (kind === 'tex') {
        // Board artwork mapped in perspective: the face is cut into tiles and
        // each tile into two triangles, each drawn with its own affine map.
        const img = texFor(spec.tex);
        if (img) {
          const iw = img.width, ih = img.height, n = Math.max(2, Math.min(4, Math.round(Math.hypot(q[2][0] - q[0][0], q[2][1] - q[0][1]) / 90)));
          for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) {
            const u0 = i / n, u1 = (i + 1) / n, v0 = j / n, v1 = (j + 1) / n;
            const A = bil(q, u0, v0), B = bil(q, u1, v0), C = bil(q, u1, v1), Dd = bil(q, u0, v1);
            tri(g, img, A, B, C, [u0 * iw, v0 * ih], [u1 * iw, v0 * ih], [u1 * iw, v1 * ih]);
            tri(g, img, A, C, Dd, [u0 * iw, v0 * ih], [u1 * iw, v1 * ih], [u0 * iw, v1 * ih]);
          }
        }
      } else if (kind === 'pins') {
        // Socket / header holes along the long side.
        const long = Math.hypot(q[1][0] - q[0][0], q[1][1] - q[0][1]) >= Math.hypot(q[3][0] - q[0][0], q[3][1] - q[0][1]);
        const n = spec.n || 10, r = spec.r || 1, a0 = bil(q, 0, 0), a1 = bil(q, long ? 1 : 0, long ? 0 : 1);
        const rad = Math.max(0.5, Math.hypot(a1[0] - a0[0], a1[1] - a0[1]) / n * 0.22);
        for (let i = 0; i < n; i++) for (let k = 0; k < r; k++) {
          const tp = (i + 0.5) / n, o = r === 1 ? 0.5 : 0.28 + k * 0.44;
          const p = long ? bil(q, tp, o) : bil(q, o, tp);
          g.fillStyle = 'rgba(0,0,0,.85)'; g.beginPath(); g.arc(p[0], p[1], rad, 0, Math.PI * 2); g.fill();
          g.fillStyle = 'rgba(214,178,94,.8)'; g.beginPath(); g.arc(p[0], p[1], rad * 0.45, 0, Math.PI * 2); g.fill();
        }
      } else if (kind === 'chipTop') {
        const d = bil(q, 0.14, 0.2);
        g.fillStyle = 'rgba(255,255,255,.22)'; g.beginPath(); g.arc(d[0], d[1], 1, 0, Math.PI * 2); g.fill();
        g.strokeStyle = 'rgba(255,255,255,.16)'; g.lineWidth = 0.8;
        [[0.3, 0.45, 0.78], [0.3, 0.6, 0.62]].forEach((r) => { const a = bil(q, r[0], r[1]), b = bil(q, r[2], r[1]); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke(); });
      } else if (kind === 'led5050') {
        const poly = (pts) => { g.beginPath(); pts.forEach((uv, i) => { const p = bil(q, uv[0], uv[1]); if (i) g.lineTo(p[0], p[1]); else g.moveTo(p[0], p[1]); }); g.closePath(); };
        g.fillStyle = 'rgba(250,236,170,.55)';
        poly([...Array(20)].map((_, i) => [0.5 + Math.cos(i / 20 * Math.PI * 2) * 0.36, 0.5 + Math.sin(i / 20 * Math.PI * 2) * 0.36])); g.fill();
        g.fillStyle = 'rgba(40,40,40,.6)'; poly([[0.44, 0.44], [0.56, 0.44], [0.56, 0.56], [0.44, 0.56]]); g.fill();
      } else if (kind === 'tact') {
        let x = 0, y = 0; q.forEach((p) => { x += p[0]; y += p[1]; });
        const c0 = [x / 4, y / 4], a = bil(q, 0.5, 0.2);
        g.fillStyle = 'rgba(70,72,78,1)'; g.beginPath(); g.arc(c0[0], c0[1], Math.hypot(a[0] - c0[0], a[1] - c0[1]), 0, Math.PI * 2); g.fill();
      } else if (kind === 'usbTop' || kind === 'indTop') {
        g.strokeStyle = kind === 'usbTop' ? 'rgba(90,96,108,.6)' : 'rgba(120,124,132,.35)'; g.lineWidth = 0.8;
        const a = bil(q, 0.12, 0.12), b = bil(q, 0.88, 0.12), c = bil(q, 0.88, 0.88), d = bil(q, 0.12, 0.88);
        g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.lineTo(c[0], c[1]); g.lineTo(d[0], d[1]); g.closePath(); g.stroke();
      } else if (kind === 'brand') {
        // Text set on the face itself: the canvas transform maps a 300 x 50
        // local box onto the face's projected u and v axes, so the lettering
        // turns and foreshortens with the plate instead of staying upright.
        const c = bil(q, 0.5, 0.5), pu = bil(q, 0.6, 0.5), pv = bil(q, 0.5, 0.6);
        const ux = (pu[0] - c[0]) * 10, uy = (pu[1] - c[1]) * 10, vx = (pv[0] - c[0]) * 10, vy = (pv[1] - c[1]) * 10;
        if (Math.abs(ux * vy - uy * vx) > 4) {
          g.transform(ux / 300, uy / 300, vx / 50, vy / 50, c[0], c[1]);
          g.fillStyle = 'rgba(255,255,255,.5)';
          g.font = '600 30px Inter, sans-serif';
          g.textAlign = 'center'; g.textBaseline = 'middle';
          g.fillText('JIANWEI', 0, 1);
        }
      }
      g.restore();
    }
  }
  Twin.all = new Set();
  window.Twin3D = { Twin, MODULES };
})();
