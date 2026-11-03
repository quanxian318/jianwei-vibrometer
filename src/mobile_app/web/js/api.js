// Device access: API v1 over the LAN (AX7010) or the Nano ESP32 Wi-Fi gateway,
// plus a simulated device for demo mode. See 15_手机APP_20261025/README.md.
(function () {
  const TIMEOUT = 6000;

  async function http(method, url, body, timeout) {
    const ctl = typeof AbortController !== 'undefined' ? new AbortController() : null;
    const timer = setTimeout(() => ctl && ctl.abort(), timeout || TIMEOUT);
    try {
      const r = await fetch(url, {
        method, signal: ctl && ctl.signal, cache: 'no-store',
        headers: body != null ? { 'Content-Type': 'application/json' } : undefined,
        body: body != null ? JSON.stringify(body) : undefined,
      });
      let j = null;
      try { j = await r.json(); } catch (e) { j = { ok: false, error: 'bad_reply' }; }
      if (j && typeof j === 'object') j.status = r.status;
      return j;
    } catch (e) {
      return { ok: false, error: 'network', status: 0 };
    } finally {
      clearTimeout(timer);
    }
  }

  class Device {
    // entry: {kind: 'ax7010' | 'gateway', host, port, name, id}
    constructor(entry) {
      this.entry = entry;
      this.kind = entry.kind;
      this.base = 'http://' + entry.host + (entry.port && entry.port !== 80 ? ':' + entry.port : '') + '/api/v1/';
    }
    get lan() { return this.kind === 'ax7010'; }
    get caps() {
      const lan = this.lan;
      return { preview: lan, files: lan, fullRecords: lan, roi: lan, fullStatus: lan, nanoNet: lan, gatewayNet: !lan, shutdown: true };
    }
    get(p) { return http('GET', this.base + p); }
    post(p, b) { return http('POST', this.base + p, b || {}); }
    async info() { return this.get('info'); }
    async live() { return this.get('live?wave=1'); }
    async records(limit) { return this.get('records?limit=' + (limit || 200)); }
    async record(id) { return this.get('records/' + encodeURIComponent(id) + '?points=1600'); }
    async settings() { return this.get('settings'); }
    async points() { return this.get('points'); }
    async status() { return this.get('status'); }
    async net() { return this.get('net'); }
    async setNet(b) { return this.post('net', b); }
    async scan() { return this.get('net/scan'); }
    previewUrl(w) { return this.base + 'preview.bmp?w=' + (w || 480) + '&_=' + Date.now(); }
    fileUrl(id, name) { return this.base + 'records/' + encodeURIComponent(id) + '/files/' + name; }
    async fetchText(url) {
      try { const r = await fetch(url, { cache: 'no-store' }); return r.ok ? await r.text() : null; } catch (e) { return null; }
    }
    // Commands: synchronous on the LAN; through the gateway the reply is polled.
    async command(cmd, args) {
      const body = Object.assign({ cmd }, args || {});
      const r = await this.post('command', body);
      if (!this.lan && r && r.status === 202 && r.data && r.data.cid) {
        const t0 = Date.now();
        while (Date.now() - t0 < 16000) {
          await U.sleep(250);
          const p = await this.get('command/' + r.data.cid);
          if (p && p.ok && p.data && p.data.done) return p.data.result || { ok: false, error: 'timeout' };
          if (p && p.status === 404) break;
        }
        return { ok: false, error: 'timeout' };
      }
      return r || { ok: false, error: 'network' };
    }
  }

  // ---------------------------------------------------------------- discovery
  // Native: UDP broadcast "VIBMON_DISCOVER" (port 48300). Everywhere: probe the
  // gateway's fixed access-point address and the devices used before.
  const Discovery = {
    async probe(host, port) {
      const base = 'http://' + host + (port && port !== 80 ? ':' + port : '');
      const r = await http('GET', base + '/api/v1/info', null, 2200);
      if (r && r.ok && r.data && r.data.service === 'vibmon') {
        const d = r.data;
        return { kind: d.kind, host, port: d.http_port || port || 80, name: d.name || d.device_id, id: d.device_id, version: d.version, ax: d.ax_online };
      }
      return null;
    },
    native(timeoutMs) {
      return new Promise((resolve) => {
        if (!window.VibmonNative || !VibmonNative.discover) return resolve([]);
        const cb = 'disc' + Date.now();
        window.__jw = window.__jw || {};
        window.__jw[cb] = (json) => {
          delete window.__jw[cb];
          try {
            resolve(JSON.parse(json).map((x) => ({ kind: x.kind, host: x.ip, port: x.http_port || 80, name: x.name || x.device_id, id: x.device_id, version: x.version, ax: x.ax_online })));
          } catch (e) { resolve([]); }
        };
        try { VibmonNative.discover(timeoutMs, cb); } catch (e) { resolve([]); }
      });
    },
    async scan(known) {
      const tasks = [this.native(1800), this.probe('192.168.4.1', 80)];
      (known || []).forEach((k) => tasks.push(this.probe(k.host, k.port)));
      const res = await Promise.all(tasks);
      const out = [];
      const seen = {};
      res.flat().forEach((d) => {
        if (!d) return;
        const key = d.host + ':' + d.port;
        if (!seen[key]) { seen[key] = 1; out.push(d); }
      });
      // A direct LAN connection first: it reaches everything.
      out.sort((a, b) => (a.kind === 'ax7010' ? 0 : 1) - (b.kind === 'ax7010' ? 0 : 1));
      return out;
    },
  };

  window.Api = { Device, Discovery, http };
})();
