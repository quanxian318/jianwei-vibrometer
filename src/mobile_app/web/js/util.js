// Small helpers shared by every page.
(function () {
  const U = {};
  U.$ = (sel, root) => (root || document).querySelector(sel);
  U.$$ = (sel, root) => Array.from((root || document).querySelectorAll(sel));
  U.esc = (s) => String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  U.sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  U.clamp = (v, a, b) => Math.max(a, Math.min(b, v));

  // Numbers: significant digits that read well on a phone.
  U.num = function (v, digits) {
    if (v == null || !isFinite(v)) return '—';
    const a = Math.abs(v);
    if (digits != null) return v.toFixed(digits);
    if (a === 0) return '0';
    if (a >= 1000) return v.toFixed(0);
    if (a >= 100) return v.toFixed(1);
    if (a >= 10) return v.toFixed(2);
    if (a >= 1) return v.toFixed(3);
    return v.toPrecision(3);
  };
  U.hz = (v) => (v == null || !isFinite(v) || v <= 0 ? '—' : v.toFixed(v >= 100 ? 1 : 2));

  U.LEVEL = {
    normal: { t: '正常', c: 'ok' },
    warn: { t: '预警', c: 'warn' },
    alarm: { t: '报警', c: 'alarm' },
    unknown: { t: '未判级', c: 'unknown' },
  };
  U.level = (lv) => U.LEVEL[lv] || U.LEVEL.unknown;
  U.levelPill = (lv) => { const l = U.level(lv); return '<span class="pill ' + l.c + '"><span class="led"></span>' + l.t + '</span>'; };
  U.STATE = { stopped: '相机未启动', preview: '就绪', capturing: '采集中', analyzing: '分析中' };

  // Device error codes -> operator text.
  const ERR = {
    already_measuring: '设备正在测量', measuring: '测量进行中，暂不能执行', camera_not_ready: '相机未就绪',
    power: '电源异常，已禁止测量', power_budget: '整机电流接近上限，已拒绝开启', power_fail: '掉电预警中', printing: '打印机正在工作',
    baseline_required: '多点测振需先在停机时采集基线', baseline_not_used: '当前方法不使用基线', capture_refused: '采集未能启动',
    storage: '存储不可用', no_record: '没有找到该记录', record_unreadable: '记录无法读取', invalid_value: '参数超出允许范围',
    nano_offline: '协处理器离线', no_carrier: '载板未连接，外设不可用', shutting_down: '设备正在关机', stereo_off: '双目未启用',
    no_depth: '暂无深度图', offset_too_large: '偏差过大，请检查目标与距离', command_not_allowed: '当前连接方式不支持该操作',
    ax_offline: '主控未连接网关', timeout: '设备响应超时', lan_only: '该内容仅在局域网直连时可用', network: '无法连接设备',
    no_data: '设备尚未上报数据', busy: '网关繁忙，请稍后', bad_station: 'Wi-Fi 名称或密码格式不正确',
    bad_ap_password: '热点密码需 8–63 个字符', ap_needed_without_station: '未配置 Wi-Fi 时需保留热点', nvs_write: '网关保存失败',
    unknown_command: '设备不支持该指令', 'unknown command': '设备不支持该指令', lights_on: '照明开启时不打印', no_preview: '暂无画面',
    unpaired: '需要先与设备配对', pin_wrong: '配对码不正确', pin_expired: '配对码已过期，请重新获取',
  };
  U.errText = (e) => (e ? ERR[e] || e : '操作失败');

  U.timeText = function (startLocal, id) {
    if (startLocal) return startLocal.replace('T', ' ').slice(0, 16);
    return '时间未同步';
  };
  U.dayKey = (startLocal) => (startLocal ? startLocal.slice(0, 10) : '未同步时间');
  U.dayLabel = function (key) {
    if (key === '未同步时间') return key;
    const d = new Date(key + 'T00:00:00');
    const today = new Date(); today.setHours(0, 0, 0, 0);
    const diff = Math.round((today - d) / 86400000);
    if (diff === 0) return '今天';
    if (diff === 1) return '昨天';
    const w = '日一二三四五六'[d.getDay()];
    return (d.getMonth() + 1) + '月' + d.getDate() + '日 · 周' + w;
  };
  U.ago = function (ms) {
    const s = Math.round((Date.now() - ms) / 1000);
    if (s < 60) return '刚刚';
    if (s < 3600) return Math.round(s / 60) + ' 分钟前';
    if (s < 86400) return Math.round(s / 3600) + ' 小时前';
    return Math.round(s / 86400) + ' 天前';
  };

  // Toast + haptics.
  let toastTimer = 0;
  U.toast = function (text, kind) {
    let t = U.$('.toast');
    if (!t) { t = document.createElement('div'); t.className = 'toast'; document.body.appendChild(t); }
    const ic = kind === 'err' ? 'alert' : kind === 'warn' ? 'bell' : 'check';
    t.className = 'toast ' + (kind || 'ok');
    t.innerHTML = Icon(ic, 's') + '<span>' + U.esc(text) + '</span>';
    requestAnimationFrame(() => t.classList.add('show'));
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => t.classList.remove('show'), 2600);
  };
  U.haptic = function (ms) {
    try { if (window.VibmonNative) VibmonNative.vibrate(ms || 12); else if (navigator.vibrate) navigator.vibrate(ms || 12); } catch (e) { /* none */ }
  };

  // Bottom sheet; resolves with the value passed to close().
  U.sheet = function (html, onMount) {
    return new Promise((resolve) => {
      const mask = document.createElement('div'); mask.className = 'mask';
      const sh = document.createElement('div'); sh.className = 'sheet';
      sh.innerHTML = '<div class="grab"></div>' + html;
      document.body.appendChild(mask); document.body.appendChild(sh);
      let done = false;
      const close = (v) => {
        if (done) return; done = true;
        mask.classList.remove('show'); sh.classList.remove('show');
        setTimeout(() => { mask.remove(); sh.remove(); }, 380);
        U.sheet.current = null;
        resolve(v);
      };
      U.sheet.current = close;
      mask.onclick = () => close(null);
      sh.addEventListener('click', (e) => {
        const b = e.target.closest('[data-close]');
        if (b) close(b.getAttribute('data-close') === '' ? true : b.getAttribute('data-close'));
      });
      requestAnimationFrame(() => { mask.classList.add('show'); sh.classList.add('show'); });
      if (onMount) onMount(sh, close);
    });
  };
  U.confirm = function (title, text, okText, danger) {
    return U.sheet('<h2>' + U.esc(title) + '</h2><p>' + U.esc(text) + '</p><div class="actions">' +
      '<button class="btn" data-close="no">取消</button><button class="btn ' + (danger ? 'danger' : 'primary') + '" data-close="yes">' + U.esc(okText || '确定') + '</button></div>')
      .then((v) => v === 'yes');
  };

  // Persisted preferences.
  U.store = {
    get(k, d) { try { const v = localStorage.getItem('jw.' + k); return v == null ? d : JSON.parse(v); } catch (e) { return d; } },
    set(k, v) { try { localStorage.setItem('jw.' + k, JSON.stringify(v)); } catch (e) { /* private mode */ } },
  };
  window.U = U;
})();
