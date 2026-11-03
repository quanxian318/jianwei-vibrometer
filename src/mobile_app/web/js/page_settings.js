// Settings (device "set" command + app preferences), network, about/privacy.
(function () {
  function sw(id, on) { return '<label class="switch"><input type="checkbox" id="' + id + '"' + (on ? ' checked' : '') + '><i></i></label>'; }
  function row(icon, title, desc, end, cls) {
    return '<div class="row static"><div class="ic ' + (cls || 'accent') + '">' + Icon(icon) + '</div><div class="main"><div class="title">' + title + '</div>' + (desc ? '<div class="desc">' + desc + '</div>' : '') + '</div>' + (end || '') + '</div>';
  }
  function slider(id, min, max, step, v, label) {
    return '<div class="row static" style="flex-direction:column;align-items:stretch;gap:2px"><div class="flex"><span class="grow">' + label + '</span><b class="num" id="' + id + 'V">' + v + '</b></div>' +
      '<input type="range" id="' + id + '" min="' + min + '" max="' + max + '" step="' + step + '" value="' + v + '"></div>';
  }
  function stepper(id, v, unit) {
    return '<div class="stepper" data-st="' + id + '"><button data-d="-1">−</button><span class="v num" id="' + id + '">' + v + ' ' + unit + '</span><button data-d="1">+</button></div>';
  }

  const Settings = {
    render(el, params) {
      this.el = el;
      el.innerHTML = App.pushHeader('设置') + '<div id="sBody"><div class="skeleton" style="height:300px"></div></div>';
      this.load();
    },
    async load() {
      const r = await App.dev.settings();
      this.s = r && r.ok ? r.data : App.settings || {};
      App.settings = this.s;
      this.draw();
    },
    draw() {
      const s = this.s;
      const theme = U.store.get('theme', 'dark');
      const has = (k) => s[k] != null;
      let h = '';
      h += '<div class="section-title">测量</div><div class="list">';
      if (has('duration_s')) h += slider('sDur', 2, 120, 1, s.duration_s, '默认采集时长 (s)');
      if (has('axis')) h += row('target', '测量方向', null, '<div class="seg" style="width:120px" id="sAxis"><button data-a="x"' + (s.axis === 'x' ? ' class="on"' : '') + '>X</button><button data-a="y"' + (s.axis === 'y' ? ' class="on"' : '') + '>Y</button></div>');
      if (has('roi_width_px')) h += row('grid', '测量框', null, '<span class="num" style="color:var(--text-2)">' + s.roi_width_px + ' × ' + s.roi_height_px + '</span>', '');
      h += '</div><div class="section-title">报警阈值（峰峰值）</div><div class="list">';
      if (has('warn_pp')) h += row('bell', '预警', null, stepper('sWarn', s.warn_pp, 'mm'), '');
      if (has('alarm_pp')) h += row('alert', '报警', null, stepper('sAlarm', s.alarm_pp, 'mm'), '');
      if (has('buzzer')) h += row('bell', '蜂鸣器', null, sw('sBuzz', s.buzzer));
      h += '</div><div class="section-title">照明与投影</div><div class="list">';
      if (has('auto_light')) h += row('light', '测量时自动照明', null, sw('sAuto', s.auto_light));
      if (has('light_percent')) h += slider('sPct', 5, 100, 5, s.light_percent, '照明亮度 (%)');
      if (has('laser_off_during_measurement')) h += row('laser', '测量时关闭十字投影', null, sw('sLaserOff', s.laser_off_during_measurement));
      h += '</div><div class="section-title">声音与指示</div><div class="list">';
      if (has('voice')) h += row('mic', '语音播报', null, sw('sVoice', s.voice));
      if (has('volume')) h += slider('sVol', 0, 100, 5, s.volume, '音量');
      if (has('led_brightness')) h += slider('sLed', 4, 255, 4, s.led_brightness, '状态灯亮度');
      if (has('fan_mode')) h += row('fan', '风扇', null, '<div class="seg" style="width:168px" id="sFan">' + [['auto', '自动'], ['on', '常开'], ['off', '关闭']].map((f) => '<button data-f="' + f[0] + '"' + (s.fan_mode === f[0] ? ' class="on"' : '') + '>' + f[1] + '</button>').join('') + '</div>');
      h += '</div><div class="section-title">APP</div><div class="list">' +
        row('moon', '外观', null, '<div class="seg" style="width:168px" id="sTheme">' + [['dark', '深色'], ['light', '浅色'], ['auto', '跟随']].map((f) => '<button data-t="' + f[0] + '"' + (theme === f[0] ? ' class="on"' : '') + '>' + f[1] + '</button>').join('') + '</div>') +
        row('clock', '自动校准设备时间', null, sw('sTime', U.store.get('autoTime', true))) + '</div>';
      U.$('#sBody', this.el).innerHTML = h;
      this.bind();
    },
    bind() {
      const el = this.el;
      const send = (k, v) => {
        clearTimeout(this.t);
        this.t = setTimeout(async () => {
          const r = await App.dev.command('set', { [k]: v });
          if (r && r.ok) { if (r.data) { this.s = r.data; App.settings = r.data; } U.toast('已保存'); App.poll(); } else { U.toast(U.errText(r && r.error), 'err'); this.load(); }
        }, 350);
      };
      const bindSlider = (id, key) => {
        const s = U.$('#' + id, el);
        if (!s) return;
        const paint = () => { s.style.setProperty('--p', ((s.value - s.min) / (s.max - s.min) * 100) + '%'); U.$('#' + id + 'V', el).textContent = s.value; };
        paint();
        s.oninput = paint;
        s.onchange = () => send(key, +s.value);
      };
      bindSlider('sDur', 'duration_s'); bindSlider('sPct', 'light_percent'); bindSlider('sVol', 'volume'); bindSlider('sLed', 'led_brightness');
      const bindSw = (id, key) => { const s = U.$('#' + id, el); if (s) s.onchange = () => send(key, s.checked); };
      bindSw('sBuzz', 'buzzer'); bindSw('sAuto', 'auto_light'); bindSw('sLaserOff', 'laser_off_during_measurement'); bindSw('sVoice', 'voice');
      const seg = (id, attr, fn) => { const s = U.$('#' + id, el); if (s) s.addEventListener('click', (e) => { const b = e.target.closest('[' + attr + ']'); if (!b) return; U.$$('button', s).forEach((x) => x.classList.toggle('on', x === b)); fn(b.getAttribute(attr)); }); };
      seg('sAxis', 'data-a', (v) => send('axis', v));
      seg('sFan', 'data-f', (v) => send('fan_mode', v));
      seg('sTheme', 'data-t', (v) => { U.store.set('theme', v); App.applyTheme(); });
      const t = U.$('#sTime', el); if (t) t.onchange = () => U.store.set('autoTime', t.checked);
      U.$$('[data-st]', el).forEach((st) => st.addEventListener('click', (e) => {
        const b = e.target.closest('[data-d]');
        if (!b) return;
        const id = st.getAttribute('data-st'), key = id === 'sWarn' ? 'warn_pp' : 'alarm_pp';
        const step = this.s[key] < 1 ? 0.1 : 0.5;
        const v = Math.max(0.1, +(this.s[key] + step * +b.getAttribute('data-d')).toFixed(2));
        if (key === 'warn_pp' && v >= this.s.alarm_pp) { U.toast('预警阈值需低于报警阈值', 'warn'); return; }
        if (key === 'alarm_pp' && v <= this.s.warn_pp) { U.toast('报警阈值需高于预警阈值', 'warn'); return; }
        this.s[key] = v;
        U.$('#' + id, el).textContent = v + ' mm';
        U.haptic(5);
        send(key, v);
      }));
    },
  };
  App.register('settings', Settings);

  // ------------------------------------------------------------------ network
  const Network = {
    render(el) {
      this.el = el;
      el.innerHTML = App.pushHeader('网络与 Wi-Fi') + '<div id="nBody"><div class="skeleton" style="height:200px"></div></div>';
      this.load();
    },
    async load() {
      const gw = !App.dev.lan || App.dev.kind === 'demo';
      let net = null;
      if (gw) { const r = await App.dev.net(); net = r && r.ok ? r.data : null; }
      const l = App.live || {}, n = l.nano || {};
      const sta = net ? net.station || {} : { state: n.wifi, ip: n.ip };
      const ap = (sta && sta.ap) || null;
      let h = '<div class="card"><div class="flex"><div class="ic" style="width:46px;height:46px;border-radius:15px;display:flex;align-items:center;justify-content:center;background:var(--accent-soft);color:var(--accent)">' + Icon('wifi') + '</div>' +
        '<div class="grow"><div class="muted" style="font-size:12px">Wi-Fi（ESP32）</div><div style="font-size:18px;font-weight:700">' + U.esc(sta.state === 'CONNECTED' ? (sta.ssid || '已连接') : sta.state === 'OFF' ? '未配置' : (sta.state || '未知')) + '</div>' +
        '<div class="muted num" style="font-size:12.5px">' + U.esc(sta.ip || '') + (sta.rssi ? ' · ' + sta.rssi + ' dBm' : '') + '</div></div></div></div>';
      if (ap) h += '<div class="list">' + row('stack', '设备热点 ' + U.esc(ap.ssid || ''), ap.enabled ? '已开启 · ' + (ap.ip || '192.168.4.1') + ' · ' + (ap.clients || 0) + ' 台手机' : '已关闭', sw('nAp', ap.enabled)) +
        '<div class="row" id="nApPw"><div class="ic">' + Icon('shield') + '</div><div class="main"><div class="title">修改热点密码</div></div>' + Icon('chev', 'chev') + '</div></div>';
      h += '<div class="section-title">连接到现场 Wi-Fi' + (gw ? '<span class="right" id="nScan">' + Icon('refresh', 's') + '</span>' : '') + '</div>';
      h += '<div class="list" id="nList">' + (gw ? '<div class="row static"><div class="main"><div class="skeleton" style="height:22px"></div></div></div>' : '') + '</div>';
      h += '<button class="btn block" id="nManual">' + Icon('edit', 's') + '手动输入网络</button>';
      U.$('#nBody', this.el).innerHTML = h;
      U.$('#nManual', this.el).onclick = () => this.join('');
      const scan = U.$('#nScan', this.el); if (scan) scan.onclick = () => this.scan();
      const apSw = U.$('#nAp', this.el);
      if (apSw) apSw.onchange = async () => {
        const r = await App.dev.setNet({ ap_enabled: apSw.checked });
        if (r && r.ok) U.toast(apSw.checked ? '热点已开启' : '热点已关闭'); else { U.toast(U.errText(r && r.error), 'err'); apSw.checked = !apSw.checked; }
      };
      const pw = U.$('#nApPw', this.el); if (pw) pw.onclick = () => this.apPassword();
      U.$('#nList', this.el).addEventListener('click', (e) => { const r = e.target.closest('[data-ssid]'); if (r) this.join(r.getAttribute('data-ssid')); });
      if (gw) this.scan();
    },
    async scan() {
      const box = U.$('#nList', this.el);
      for (let i = 0; i < 6; i++) {
        const r = await App.dev.scan();
        const d = r && r.ok ? r.data : null;
        if (d && d.networks && d.networks.length) {
          box.innerHTML = d.networks.map((n) => '<div class="row" data-ssid="' + U.esc(n.ssid) + '"><div class="ic">' + Icon('wifi') + '</div><div class="main"><div class="title">' + U.esc(n.ssid || '(隐藏网络)') + '</div><div class="desc">' + (n.secure ? '加密' : '开放') + ' · 信号 ' + n.rssi + ' dBm</div></div>' +
            '<div class="end">' + bars(n.rssi) + '</div></div>').join('');
          return;
        }
        await U.sleep(1200);
      }
      box.innerHTML = '<div class="row static"><div class="main"><div class="title">未扫描到网络</div><div class="desc">可手动输入网络名称</div></div></div>';
    },
    join(ssid) {
      U.sheet('<h2>连接 Wi-Fi</h2><label class="field"><span>网络名称</span><input class="input" id="jS" autocomplete="off"></label>' +
        '<label class="field"><span>密码（开放网络留空）</span><input class="input" id="jP" type="password" autocomplete="off"></label>' +
        '<div class="actions"><button class="btn" data-close="">取消</button><button class="btn primary" id="jGo">连接</button></div>', (sh, close) => {
        U.$('#jS', sh).value = ssid;
        U.$('#jGo', sh).onclick = async () => {
          const s = U.$('#jS', sh).value.trim(), p = U.$('#jP', sh).value;
          if (!s) return;
          if (p && p.length < 8) { U.toast('密码至少 8 位', 'warn'); return; }
          const gw = !App.dev.lan || App.dev.kind === 'demo';
          const r = gw ? await App.dev.setNet({ ssid: s, password: p }) : await App.dev.command('set_net', { ssid: s, password: p, persist: true });
          if (r && r.ok) { U.toast('已发送，正在连接 ' + s); close(); setTimeout(() => this.load(), 4000); } else U.toast(U.errText(r && r.error), 'err');
        };
      });
    },
    apPassword() {
      U.sheet('<h2>修改热点密码</h2><p>修改后已连接的手机需用新密码重新连接热点。</p><label class="field"><span>新密码（8–63 位）</span><input class="input" id="aP" autocomplete="off"></label>' +
        '<div class="actions"><button class="btn" data-close="">取消</button><button class="btn primary" id="aGo">保存</button></div>', (sh, close) => {
        U.$('#aGo', sh).onclick = async () => {
          const p = U.$('#aP', sh).value;
          if (p.length < 8 || p.length > 63) { U.toast('密码需 8–63 位', 'warn'); return; }
          const r = await App.dev.setNet({ ap_password: p });
          if (r && r.ok) { U.toast('热点密码已修改'); close(); } else U.toast(U.errText(r && r.error), 'err');
        };
      });
    },
  };
  function bars(rssi) {
    const n = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
    let h = '<svg width="20" height="16" viewBox="0 0 20 16">';
    for (let i = 0; i < 4; i++) h += '<rect x="' + (i * 5) + '" y="' + (12 - i * 4) + '" width="3.4" height="' + (4 + i * 4) + '" rx="1" fill="' + (i < n ? 'var(--accent)' : 'var(--border-strong)') + '"/>';
    return h + '</svg>';
  }
  App.register('network', Network);

  // ------------------------------------------------------------------ about
  const About = {
    render(el) {
      const l = App.live || {};
      el.innerHTML = App.pushHeader('关于') +
        '<div class="brand" style="padding-top:10px">' + Logo(84) + '<h1 style="font-size:24px">见微测振</h1><p>版本 ' + App.version + '</p></div>' +
        '<div class="list">' + row('device', '设备软件', null, '<span class="muted num">' + U.esc(l.ver || '—') + '</span>', '') +
        row('link', '连接方式', null, '<span class="muted">' + (App.dev ? (App.dev.kind === 'demo' ? '演示' : App.dev.lan ? '局域网直连' : 'Wi-Fi 网关') : '—') + '</span>', '') +
        '<div class="row" id="abPriv"><div class="ic">' + Icon('shield') + '</div><div class="main"><div class="title">隐私政策</div></div>' + Icon('chev', 'chev') + '</div>' +
        '<div class="row" id="abTerms"><div class="ic">' + Icon('file') + '</div><div class="main"><div class="title">用户协议</div></div>' + Icon('chev', 'chev') + '</div></div>' +
        '<div class="card doc"><h4 style="margin-top:0">见微 · 看见细微振动</h4>见微测振仪用相机非接触地测量设备表面的振动位移与频率，并结合双目深度、声学包络和环境传感器记录测量条件。本 APP 用于在手机上查看实时状态、远程启动测量、浏览与导出记录、管理 NFC 测点和配置设备。</div>' +
        '<div class="center-note">© 2026 见微团队 · 仅在本地网络通信</div>';
      U.$('#abPriv', el).onclick = () => App.push('doc', { kind: 'privacy' });
      U.$('#abTerms', el).onclick = () => App.push('doc', { kind: 'terms' });
    },
  };
  App.register('about', About);

  const Doc = {
    render(el, p) {
      const privacy = p.kind === 'privacy';
      el.innerHTML = App.pushHeader(privacy ? '隐私政策' : '用户协议') + '<div class="card doc">' + (privacy ?
        '<h4 style="margin-top:0">我们收集什么</h4>见微测振 APP 不注册账号、不收集个人信息、不接入任何第三方统计或广告 SDK。' +
        '<h4>数据去向</h4>APP 只与您所在局域网或设备热点中的见微测振仪直接通信。测量数据保存在设备上；只有您主动导出时才会写入手机的「下载」目录。' +
        '<h4>本机存储</h4>APP 在本机保存最近连接的设备地址、外观与时长偏好，卸载 APP 即全部清除。' +
        '<h4>权限说明</h4>网络：访问局域网中的设备；Wi-Fi 组播：在局域网中发现设备；振动：报警触感提示。APP 不申请定位、相机、通讯录等权限。' :
        '<h4 style="margin-top:0">适用范围</h4>本 APP 用于配合见微测振仪进行现场振动测量与设备管理。' +
        '<h4>测量结果</h4>测量值受机位、标定、光照、表面纹理等条件影响。未标定时结果以像素表示，不作毫米判级；未通过质量检查的结果仅供参考。报警阈值应由使用单位按设备标准设定。' +
        '<h4>安全</h4>远程启动测量、开关照明与投影、关机等操作会在现场设备上立即执行，请确认现场人员安全。十字投影为激光光源，请勿直视。' +
        '<h4>网络</h4>设备热点与局域网接口面向可信的内部网络，请勿将设备直接暴露到公网。') + '</div>';
    },
  };
  App.register('doc', Doc);
})();
