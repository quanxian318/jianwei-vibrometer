// 设备: 3D digital twin (explode, tap a module for its live status), health
// summary, system figures and the entry to settings, network, time, about.
(function () {
  const DESC = {
    core: 'Zynq-7010 主控：PL 实时采集与双目匹配，PS Linux 运行测量、记录、界面与网络服务。',
    carrier: '外围主载板：12 V 输入、TVS 与滤波、INA226 功率监测、负载 MOS、打印/风扇/照明驱动。',
    camera: 'AN5642 双目相机：左目用于位移跟踪，左右配对生成深度图。',
    lcd: '微雪 7 寸 HDMI 触摸屏：现场操作界面，与手机 APP 同步显示。',
    light: 'LED1 照明灯：PWM 调光，测量时可自动开启。',
    laser: 'LED2 十字线投影：辅助少纹理表面对位，位移测量时自动关闭。',
    nano: 'Arduino Nano ESP32：NFC、辅助 SD、语音播报、Wi-Fi/NTP 与手机网关。',
    nfc: 'PN532 NFC：刷测点标签自动切换测点。',
    sensors: 'LSM6DSV 惯性、VL53L1X 测距、SHT40 温湿度：记录采集条件。',
    mic: 'MP34DT01 PDM 麦克风：声学包络对照视觉主频，只作佐证。',
    panel: '前面板：EC11 旋钮、K1 按键与 WS2812 状态灯。',
    printer: 'EM5820H 热敏打印机：现场打印测量报告。',
    power: '12 V/5 A 输入与功率监测：过流、掉电预警时保护数据。',
    fan: '散热风扇：按温度自动启停，测量时可暂停以免引入振动。',
  };
  const MOD_IC = { core: 'chip', carrier: 'layers', camera: 'camera', lcd: 'device', light: 'light', laser: 'laser', nano: 'wifi', nfc: 'nfc', sensors: 'temp', mic: 'mic', panel: 'target', printer: 'print', power: 'bolt', fan: 'fan' };
  const ST_TEXT = { ok: '正常', warn: '注意', alarm: '故障', off: '关闭', unknown: '未知' };

  function statusMap(l) {
    const m = {};
    const s = (state, text, rows) => ({ state, text, rows: rows || [] });
    if (!l) { Twin3D.MODULES.forEach((x) => { m[x.id] = s('unknown', '未连接'); }); return m; }
    const pw = l.pwr || {}, nano = l.nano || {}, st = l.st || {}, light = l.light || {};
    m.core = s(App.online ? 'ok' : 'warn', App.online ? '运行中' : '连接中断', [['软件版本', l.ver], ['相机帧率', l.fps ? U.num(l.fps, 1) + ' fps' : '—'], ['时钟', l.tv ? '已同步' : '未同步']]);
    m.carrier = pw.pf ? s('alarm', '掉电预警') : pw.v > 0 ? s(pw.warn ? 'warn' : 'ok', U.num(pw.v, 2) + ' V', [['输入电压', U.num(pw.v, 2) + ' V'], ['整机电流', U.num(pw.a, 2) + ' A'], ['功率', U.num(pw.w, 1) + ' W']]) : s('unknown', '未检测到载板', [['说明', '裸板运行或载板未接']]);
    m.camera = l.cam ? s('ok', (l.fw || '') + '×' + (l.fh || '') + ' · ' + U.num(l.fps, 1) + ' fps', [['分辨率', l.fw + ' × ' + l.fh], ['帧率', U.num(l.fps, 1) + ' fps'], ['双目深度', st.on ? (st.be || '') + ' · 有效 ' + Math.round((st.vf || 0) * 100) + '%' : '未启用']]) : s('alarm', '相机未就绪');
    m.lcd = s(App.online ? 'ok' : 'unknown', '现场界面', [['分辨率', '1024 × 600'], ['触控', '电容触摸']]);
    m.light = light.on ? s('ok', '亮度 ' + U.num(light.pct, 0) + '%', [['状态', '开启'], ['亮度', U.num(light.pct, 0) + '%']]) : !light.ok && pw.v > 0 ? s('warn', '机壳开关未允许', [['SW301', '断开']]) : s('off', '关闭');
    m.laser = l.laser ? s('ok', '投影中') : s('off', '关闭', [['测量期间', '自动关闭']]);
    m.nano = nano.on ? s(nano.wifi === 'CONNECTED' || nano.wifi === 'OFF' ? 'ok' : 'warn', nano.wifi === 'CONNECTED' ? 'Wi-Fi ' + (nano.ip || '') : 'Wi-Fi ' + (nano.wifi || '').toLowerCase(), [['Wi-Fi', nano.wifi], ['地址', nano.ip || '—'], ['辅助 SD', nano.sd ? '已挂载' : '未插入']]) : s('alarm', '协处理器离线');
    m.nfc = nano.on ? s(nano.nfc ? 'ok' : 'warn', nano.nfc ? '就绪' : '未就绪', [['当前测点', (l.pt && l.pt.id) || '—'], ['标签 UID', (l.pt && l.pt.uid) || '—']]) : s('unknown', '—');
    m.sensors = l.tc != null || l.dist != null ? s('ok', (l.tc != null ? U.num(l.tc, 1) + ' °C ' : '') + (l.dist != null ? (l.dist / 1000).toFixed(2) + ' m' : ''), [['温度', l.tc != null ? U.num(l.tc, 1) + ' °C' : '—'], ['湿度', l.rh != null ? U.num(l.rh, 0) + ' %' : '—'], ['测距', l.dist != null ? (l.dist / 1000).toFixed(3) + ' m' : '—']]) : s('unknown', '无数据');
    const ac = l.ac || 'disabled';
    m.mic = ac === 'ready' || ac === 'capturing' ? s('ok', ac === 'capturing' ? '录制中' : '就绪') : ac === 'fault' ? s('alarm', '故障') : ac === 'disabled' || ac === 'absent' ? s('off', '未接入') : s('warn', '启动中');
    m.panel = s(l.lv === 'alarm' ? 'alarm' : l.lv === 'warn' ? 'warn' : 'ok', U.level(l.lv).t + (l.mute ? ' · 已静音' : ''), [['振动判级', U.level(l.lv).t], ['报警声', l.mute ? '已静音' : '正常']]);
    const prn = l.prn || 'idle';
    m.printer = /fail|error/.test(prn) ? s('alarm', '打印异常') : prn === 'disabled' ? s('off', '未启用') : s('ok', prn === 'idle' ? '待机' : '打印中', [['状态', prn]]);
    m.power = pw.pf ? s('alarm', '掉电预警') : pw.v > 0 ? s(pw.warn ? 'warn' : 'ok', U.num(pw.w, 1) + ' W', [['电压', U.num(pw.v, 2) + ' V'], ['电流', U.num(pw.a, 2) + ' A'], ['功率', U.num(pw.w, 1) + ' W']]) : s('unknown', '未检测');
    m.fan = l.fan ? s('ok', '运行') : s('off', '待机');
    return m;
  }

  const P = {
    render(el) {
      this.el = el;
      el.innerHTML = App.header('设备', '<button class="icon-btn" id="dvSet">' + Icon('sliders') + '</button>') +
        '<div class="dev-head">' + Logo(48) + '<div class="t"><div class="name" id="dvName">见微测振仪</div><div class="meta" data-conn></div></div></div>' +
        '<div class="twin-wrap no-swipe"><canvas id="dvTwin"></canvas>' +
        '<div class="twin-tools"><button class="icon-btn" id="dvReset">' + Icon('refresh', 's') + '</button></div></div>' +
        '<div class="explode no-swipe"><span class="ex-ic">' + Icon('layers', 's') + '</span><input type="range" id="dvEx" min="0" max="100" value="0"><b class="num" id="dvExV">0%</b></div>' +
        '<div id="dvMod"></div>' +
        '<div class="section-title">模块</div><div class="list" id="dvList"></div>' +
        '<div class="section-title">系统</div><div class="grid2" id="dvSys"></div>' +
        '<div class="section-title">管理</div>' +
        '<div class="list">' +
        '<div class="row" data-go="settings"><div class="ic accent">' + Icon('sliders') + '</div><div class="main"><div class="title">测量与报警</div></div>' + Icon('chev', 'chev') + '</div>' +
        '<div class="row" data-go="network"><div class="ic accent">' + Icon('wifi') + '</div><div class="main"><div class="title">网络与 Wi-Fi</div></div><span class="end" id="dvNet"></span>' + Icon('chev', 'chev') + '</div>' +
        '<div class="row" data-do="time"><div class="ic accent">' + Icon('clock') + '</div><div class="main"><div class="title">同步时间</div></div><span class="end" id="dvTime"></span>' + Icon('chev', 'chev') + '</div>' +
        '<div class="row" data-do="voice"><div class="ic accent">' + Icon('bell') + '</div><div class="main"><div class="title">测试提示音</div></div>' + Icon('chev', 'chev') + '</div>' +
        '</div><div class="list">' +
        '<div class="row" data-go="about"><div class="ic">' + Icon('info') + '</div><div class="main"><div class="title">关于见微测振</div></div>' + Icon('chev', 'chev') + '</div>' +
        '<div class="row" data-do="switch"><div class="ic">' + Icon('unlink') + '</div><div class="main"><div class="title">切换设备</div></div>' + Icon('chev', 'chev') + '</div>' +
        '<div class="row" data-do="shutdown"><div class="ic" style="background:var(--alarm-soft);color:var(--alarm)">' + Icon('power') + '</div><div class="main"><div class="title c-alarm">关闭设备</div></div></div>' +
        '</div>';
      const ex = U.$('#dvEx', el);
      const paint = (v) => { ex.value = Math.round(v * 100); ex.style.setProperty('--p', ex.value + '%'); U.$('#dvExV', el).textContent = ex.value + '%'; };
      this.twin = new Twin3D.Twin(U.$('#dvTwin', el), { onSelect: (id) => this.showModule(id), onExplode: (v) => paint(v) });
      paint(0);
      ex.oninput = () => { this.twin.setExplode(ex.value / 100); };
      U.$('#dvReset', el).onclick = () => { this.twin.reset(); this.twin.select(null); this.twin.setExplode(0); };
      U.$('#dvSet', el).onclick = () => App.push('settings');
      U.$('#dvList', el).addEventListener('click', (e) => {
        const r = e.target.closest('[data-m]');
        if (!r) return;
        const id = r.getAttribute('data-m');
        this.twin.select(id);
        U.$('.twin-wrap', el).scrollIntoView({ behavior: 'smooth', block: 'start' });
      });
      el.addEventListener('click', (e) => {
        const g = e.target.closest('[data-go]');
        if (g) { App.push(g.getAttribute('data-go')); return; }
        const d = e.target.closest('[data-do]');
        if (d) this.act(d.getAttribute('data-do'));
      });
      App.on((l) => this.update(l));
      this.update(App.live);
      this.showModule(null);
    },
    reset() { this.update(App.live); },
    show() { this.twin && this.twin.kick(); },
    onTheme() { this.twin && this.twin.kick(); },
    update(l) {
      const el = this.el;
      if (!el) return;
      this.sm = statusMap(l);
      this.twin.setStatus(this.sm);
      this.twin.setLive(l);
      if (l) {
        const k = l.mmpx || 1, w = (l.wave || []).slice(-90).map((v) => v * k);
        const m = w.length ? Math.max.apply(null, w.map(Math.abs)) || 1 : 1;
        this.twin.setOutputs(!!(l.light && l.light.on), !!l.laser, l.lv, !!l.fan, w.map((v) => v / m));
      }
      U.$('#dvName', el).textContent = App.dev && App.dev.kind === 'demo' ? '演示设备' : (l && l.dev) || '见微测振仪';
      const list = this.twin.modules().map((m) => {
        const s = this.sm[m.id] || { state: 'unknown', text: '' };
        const cls = s.state === 'off' ? 'unknown' : s.state;
        return '<div class="row" data-m="' + m.id + '"><div class="ic" style="background:var(--' + cls + '-soft);color:var(--' + cls + ')">' + Icon(MOD_IC[m.id] || 'chip') + '</div>' +
          '<div class="main"><div class="title">' + m.name.replace(/（.*）/, '') + '</div></div><span class="end num">' + U.esc(s.text || ST_TEXT[s.state]) + '</span></div>';
      }).join('');
      const le = U.$('#dvList', el);
      if (le.innerHTML !== list) le.innerHTML = list;
      this.showModule(this.twin.sel, true);
      if (!l) return;
      const pw = l.pwr || {};
      const sys = [['bolt', '功率', pw.w ? U.num(pw.w, 1) : '—', 'W'], ['gauge', '电压', pw.v ? U.num(pw.v, 2) : '—', 'V'],
        ['camera', '帧率', l.fps ? U.num(l.fps, 1) : '—', 'fps'], ['cube', '深度有效', l.st && l.st.on ? Math.round((l.st.vf || 0) * 100) : '—', '%']];
      const sh = sys.map((e) => '<div class="stat"><div class="k">' + Icon(e[0]) + e[1] + '</div><div class="v num">' + e[2] + '<small>' + e[3] + '</small></div></div>').join('');
      if (U.$('#dvSys', el).innerHTML !== sh) U.$('#dvSys', el).innerHTML = sh;
      const n = l.nano || {};
      U.$('#dvNet', el).textContent = n.wifi === 'CONNECTED' ? '已连接' : '未连接';
      U.$('#dvTime', el).textContent = l.tv ? '已同步' : '未同步';
    },
    // The health summary stays on the page; a selected module opens a sheet
    // with its details (refreshed with the live state while it is open).
    showModule(id) {
      const box = U.$('#dvMod', this.el);
      if (!box || !this.sm) return;
      const all = this.twin.modules();
      const c = { ok: 0, warn: 0, alarm: 0, off: 0, unknown: 0 };
      all.forEach((m) => { c[(this.sm[m.id] || {}).state || 'unknown']++; });
      const health = c.alarm ? 'alarm' : c.warn ? 'warn' : 'ok';
      const html = '<div class="card health"><div class="flex"><div class="hico" style="background:var(--' + health + '-soft);color:var(--' + health + ')">' + Icon(health === 'ok' ? 'shield' : 'alert') + '</div>' +
        '<div class="grow"><div class="h-t">' + (health === 'ok' ? '整机状态良好' : health === 'warn' ? '有模块需要注意' : '有模块异常') + '</div>' +
        '<div class="h-s num">' + c.ok + ' / ' + all.length + ' 正常</div></div></div></div>';
      if (box.innerHTML !== html) box.innerHTML = html;
      if (!id) {
        if (this.sheetId && U.sheet.current) U.sheet.current(null);
        return;
      }
      if (this.sheetId === id && this.sheetEl) {
        const body = U.$('#modBody', this.sheetEl), nh = this.moduleHtml(id);
        if (body && body.innerHTML !== nh) body.innerHTML = nh;
        return;
      }
      if (this.sheetId && U.sheet.current) U.sheet.current(null);
      this.sheetId = id;
      U.sheet('<div id="modBody">' + this.moduleHtml(id) + '</div>', (sh, close) => {
        this.sheetEl = sh;
        const mask = sh.previousSibling;
        if (mask && mask.classList && mask.classList.contains('mask')) { mask.style.background = 'rgba(9,12,20,.12)'; mask.style.backdropFilter = 'none'; }  // keep the model visible
        sh.addEventListener('click', (e) => {
          const a = e.target.closest('[data-mact]');
          if (!a) return;
          const act = a.getAttribute('data-mact');
          if (act === 'measure' || act === 'net') close(null);
          this.moduleAction(act);
        });
      }).then(() => {
        if (this.sheetId !== id) return;
        this.sheetId = null;
        this.sheetEl = null;
        if (this.twin.sel === id) this.twin.select(null);
      });
    },
    moduleHtml(id) {
      const m = Twin3D.MODULES.find((x) => x.id === id);
      const s = this.sm[id] || { state: 'unknown', rows: [] };
      const cls = s.state === 'off' ? 'unknown' : s.state;
      const l = App.live || {};
      const acts = id === 'light' ? '<button class="btn small" data-mact="light">' + (l.light && l.light.on ? '关闭照明' : '打开照明') + '</button>'
        : id === 'laser' ? '<button class="btn small" data-mact="laser">' + (l.laser ? '关闭投影' : '打开投影') + '</button>'
          : id === 'panel' ? '<button class="btn small" data-mact="mute">静音确认</button>'
            : id === 'camera' ? '<button class="btn small" data-mact="measure">去测量</button>'
              : id === 'nano' ? '<button class="btn small" data-mact="net">网络设置</button>' : '';
      return '<div class="flex"><div class="hico" style="background:var(--' + cls + '-soft);color:var(--' + cls + ')">' + Icon(MOD_IC[id] || 'chip') + '</div>' +
        '<div class="grow"><div class="h-t">' + m.name.replace(/（.*）/, '') + '</div><div class="h-s">' + ST_TEXT[s.state] + '</div></div>' +
        '<button class="icon-btn" data-close="" style="box-shadow:none;background:var(--surface-2)">' + Icon('close', 's') + '</button></div>' +
        '<div class="mod-desc">' + (DESC[id] || '') + '</div>' +
        (s.rows.length ? '<div class="kv">' + s.rows.map((r) => '<div class="k">' + r[0] + '</div><div class="v num">' + U.esc(r[1] == null ? '—' : r[1]) + '</div>').join('') + '</div>' : '') +
        (acts ? '<div class="flex mt16">' + acts + '</div>' : '');
    },
    moduleAction(a) {
      const l = App.live || {};
      if (a === 'light') App.cmd('light', { on: !(l.light && l.light.on) });
      if (a === 'laser') App.cmd('laser', { on: !l.laser });
      if (a === 'mute') App.cmd('mute', {}, '已静音');
      if (a === 'measure') App.go('measure');
      if (a === 'net') App.push('network');
    },
    async act(a) {
      if (a === 'time') {
        const r = await App.cmd('rtc_set', { unix_ms: Date.now() }, '设备时间已与手机同步');
        return r;
      }
      if (a === 'voice') return App.cmd('voice', { asset: 'measure_done' }, '已播放提示音');
      if (a === 'switch') { if (await U.confirm('切换设备', '将断开当前设备并返回搜索页。', '断开')) App.disconnect(); return; }
      if (a === 'shutdown') {
        if (await U.confirm('关闭设备？', '设备将保存记录、通知协处理器并安全断电。需要在现场重新开机。', '关闭设备', true)) App.cmd('shutdown', {}, '设备正在关机');
      }
    },
  };
  App.register('device', P);
})();
