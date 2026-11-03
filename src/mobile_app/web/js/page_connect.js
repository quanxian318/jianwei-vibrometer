// Welcome / device search / manual address / demo.
(function () {
  const P = {
    render(el) {
      this.el = el;
      el.innerHTML =
        '<div class="welcome">' +
        '<div class="brand fade-in">' + Logo(72) + '<h1>见微测振</h1><p>看见细微的振动</p></div>' +
        '<div class="stage no-swipe"><canvas id="heroTwin"></canvas></div>' +
        '<div class="scan-note" id="scanNote"></div>' +
        '<div class="list found" id="found"></div>' +
        '<div class="actions"><button class="btn primary block" id="rescan">' + Icon('search', 's') + '<span>搜索设备</span></button>' +
        '<div class="grid2 mt12"><button class="btn" id="manual">' + Icon('edit', 's') + '输入地址</button>' +
        '<button class="btn" id="demo">' + Icon('sparkle', 's') + '演示模式</button></div></div>' +
        '<div class="center-note">v' + App.version + '</div>' +
        '</div>';
      U.$('#rescan', el).onclick = () => this.scan();
      U.$('#manual', el).onclick = () => this.manual();
      U.$('#demo', el).onclick = () => { U.haptic(12); App.connect({ kind: 'demo', host: 'demo', port: 0, name: '演示设备' }); };
      U.$('#found', el).addEventListener('click', (e) => {
        const r = e.target.closest('[data-i]');
        if (r) this.pick(this.items[+r.getAttribute('data-i')]);
      });
      // A showpiece: turn it, but no taps (there is no slider here to close
      // an exploded model) and it starts spinning again after a drag.
      this.hero = new Twin3D.Twin(U.$('#heroTwin', el), { tap: false, autoFit: true, resumeMs: 1800, rxRange: [-0.75, 0.3], zoomRange: [0.75, 1.15] });
      this.hero.zoom = 0.9;
      this.hero.kick();
      this.items = [];
      this.showSaved();
      this.scan();
    },
    hide() { this.scanning = false; },
    showSaved() {
      const saved = U.store.get('devices', []);
      this.items = saved.map((d) => Object.assign({ saved: true }, d));
      this.renderFound();
    },
    renderFound() {
      const box = U.$('#found', this.el);
      if (!this.items.length) { box.style.display = 'none'; return; }
      box.style.display = '';
      box.innerHTML = this.items.map((d, i) => {
        const lan = d.kind === 'ax7010';
        const state = d.live ? '<span class="pill ok"><span class="led"></span>在线</span>' : d.saved ? '<span class="pill">最近使用</span>' : '';
        return '<div class="row fade-in" data-i="' + i + '"><div class="ic accent">' + Icon(lan ? 'lan' : 'wifi') + '</div>' +
          '<div class="main"><div class="title">' + U.esc(d.name || d.id || '见微测振仪') + '</div><div class="desc">' +
          (lan ? '局域网直连' : 'Wi-Fi 网关') + ' · ' + U.esc(d.host) + (d.port && d.port !== 80 ? ':' + d.port : '') +
          (d.kind === 'gateway' && d.ax === false ? ' · 主控未连接' : '') + '</div></div>' +
          '<div class="end">' + state + Icon('chev', 'chev') + '</div></div>';
      }).join('');
    },
    async scan() {
      if (this.scanning) return;
      this.scanning = true;
      U.$('#rescan', this.el).innerHTML = Icon('refresh', 's spin') + '<span>正在搜索</span>';
      U.$('#scanNote', this.el).textContent = '';
      const found = await Api.Discovery.scan(U.store.get('devices', []));
      this.scanning = false;
      const rb = U.$('#rescan', this.el); if (rb) rb.innerHTML = Icon('search', 's') + '<span>重新搜索</span>';
      const saved = U.store.get('devices', []);
      const map = {};
      found.forEach((d) => { map[d.host + ':' + d.port] = Object.assign({ live: true }, d); });
      saved.forEach((d) => { const k = d.host + ':' + d.port; if (!map[k]) map[k] = Object.assign({ saved: true }, d); else map[k].saved = true; });
      this.items = Object.values(map).sort((a, b) => (b.live ? 1 : 0) - (a.live ? 1 : 0));
      this.renderFound();
      const note = U.$('#scanNote', this.el);
      if (note) note.textContent = found.length ? '' : '未发现设备';
    },
    async pick(d) {
      if (!d) return;
      U.haptic(10);
      U.toast('正在连接 ' + (d.name || d.host) + '…');
      const ok = await App.connect(d);
      if (ok) U.toast('已连接 ' + (App.live ? App.live.dev : d.name));
    },
    manual() {
      U.sheet('<h2>输入设备地址</h2><p>局域网直连：AX7010 地址，端口 8080<br>设备热点：连接 VIBMON-XXXX（密码 vibmon2026）后输入 192.168.4.1</p>' +
        '<label class="field"><span>地址</span><input class="input" id="addr" placeholder="192.168.1.50:8080" inputmode="url" autocomplete="off"></label>' +
        '<div class="actions"><button class="btn" data-close="">取消</button><button class="btn primary" id="go">连接</button></div>', (sh, close) => {
        const inp = U.$('#addr', sh);
        inp.value = U.store.get('manualAddr', '');
        setTimeout(() => inp.focus(), 300);
        U.$('#go', sh).onclick = async () => {
          const v = inp.value.trim().replace(/^https?:\/\//, '').replace(/\/.*$/, '');
          if (!v) return;
          U.store.set('manualAddr', v);
          const [host, port] = v.split(':');
          const p = port ? +port : 0;
          U.$('#go', sh).innerHTML = Icon('refresh', 's spin') + '连接中';
          const info = (await Api.Discovery.probe(host, p || 8080)) || (await Api.Discovery.probe(host, p || 80));
          if (!info) { U.$('#go', sh).textContent = '连接'; U.toast('该地址没有响应', 'err'); return; }
          close();
          this.pick(info);
        };
      });
    },
  };
  App.register('connect', P);
})();
