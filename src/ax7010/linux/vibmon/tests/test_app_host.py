#!/usr/bin/env python3
"""Host regressions using synthetic frames and a memory-backed register fixture."""
import json
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import time

binary = pathlib.Path(sys.argv[1]).resolve()
failures = []

def check(ok, message):
    print(('PASS ' if ok else 'FAIL ') + message, flush=True)
    if not ok:
        failures.append(message)

class Daemon:
    def __init__(self, directory, calibrated=True, carrier=False, extra=()):
        self.dir = pathlib.Path(directory)
        self.sock = self.dir / 'ctl.sock'
        cfg = {
            'camera': {'source': 'synthetic', 'synthetic': {'fps': 60}},
            'measurement': {'roi_width_px': 48, 'roi_height_px': 48, 'search_radius_px': 6,
                            'duration_s': 1.2, 'calibration_file': str(self.dir / 'cal.json')},
            'storage': {'records_dir': str(self.dir / 'records'), 'settings_file': str(self.dir / 'settings.json')},
            'ui': {'enabled': False}, 'voice': {'enabled': False},
            'control_socket': str(self.sock), 'poweroff_on_shutdown': False,
            'imu': {'enabled': False}, 'adxl345': {'enabled': False},
            'app_api': {'enabled': False},
        }
        if calibrated:
            (self.dir / 'cal.json').write_text(json.dumps({'id': 'test', 'mm_per_px': .05, 'axis': 'y'}))
        if carrier:
            regs = bytearray(4096)
            struct.pack_into('<III', regs, 0, 0x56424331, 0x10000, 100000000)
            (self.dir / 'carrier').write_bytes(regs)  # PFAIL remains low at boot.
            if carrier == 'lighting':
                struct.pack_into('<I', regs, 0x20, 1 << 6)
                (self.dir / 'carrier').write_bytes(regs)
            cfg['hardware'] = {'devices': {'carrier_uio': str(self.dir / 'carrier')}}
        for e in extra:  # merged one level deep
            for k, v in e.items():
                cfg[k] = {**cfg.get(k, {}), **v} if isinstance(v, dict) else v
        (self.dir / 'cfg.json').write_text(json.dumps(cfg))
        self.log = (self.dir / 'daemon.log').open('w')
        self.p = subprocess.Popen([str(binary), '-c', str(self.dir / 'cfg.json')], stdout=self.log, stderr=self.log)

    def ready(self):
        for _ in range(150):
            if self.p.poll() is not None:
                return False
            if self.sock.exists():
                return True
            time.sleep(.02)
        return False

    def cmd(self, cmd, **args):
        with socket.socket(socket.AF_UNIX) as s:
            s.settimeout(4)
            s.connect(str(self.sock))
            s.sendall((json.dumps(dict(cmd=cmd, **args)) + '\n').encode())
            data = b''
            while b'\n' not in data:
                data += s.recv(65536)
            return json.loads(data)

    def result(self, polls=200):
        for _ in range(polls):
            st = self.cmd('status')['data']
            last = st['measurement'].get('last')
            if last:
                return last
            time.sleep(.03)
        raise AssertionError('measurement did not finish')

    def close(self):
        if self.p.poll() is None:
            self.p.terminate()
            try:
                self.p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.p.kill()
                self.p.wait()
        self.log.close()

with tempfile.TemporaryDirectory(prefix='vibmon-regression-') as temp:
    root = pathlib.Path(temp)
    for name in ['point', 'pixel', 'startup', 'lighting', 'laser', 'shutdown', 'settings']:
        d = root / name
        d.mkdir()
        app = Daemon(d, calibrated=name != 'pixel', carrier={'startup': 'startup', 'lighting': 'lighting', 'laser': 'lighting'}.get(name, False))
        try:
            ready = app.ready()
            check(ready, name + ': starts without crashing')
            if not ready:
                print((d / 'daemon.log').read_text()[-1800:])
                continue
            if name == 'startup':
                check(app.cmd('status')['data']['power']['pfail'], 'PFAIL low at boot is retained')
            elif name == 'lighting':
                app.cmd('measure_start')
                check(app.cmd('status')['data']['light']['on'], 'automatic light is on during capture')
                last = app.result()
                rec = json.loads((d / 'records' / last['measurement_id'] / 'record.json').read_text())
                check(rec['lighting']['on'], 'record retains automatic light state after it is switched off')
            elif name == 'point':
                app.cmd('set_point', id='A', name='point A')
                check(app.cmd('roi', x=0.5, y=0.5, w=80, h=40)['ok'], 'measuring box accepts width and height')
                check(not app.cmd('roi', w=8)['ok'] and app.cmd('settings')['data']['roi_width_px'] == 80,
                      'a box below 16 px is refused and leaves the size unchanged')
                check(app.cmd('measure_start')['ok'], 'capture accepted')
                time.sleep(.25)
                app.cmd('set_point', id='B', name='point B')
                check(not app.cmd('roi', w=64, h=64)['ok'], 'capture refuses resizing the measuring box')
                last = app.result()
                rec = json.loads((d / 'records' / last['measurement_id'] / 'record.json').read_text())
                check(rec['point_id'] == 'A' and last['valid'], 'record keeps the point selected at capture start')
                check(last['peak_clear'] is True and rec['result']['peak_clear'] is True,
                      'a clean line is reported as a clear dominant frequency')
                check((rec['roi']['w'], rec['roi']['h']) == (80, 40), 'capture tracks the box size set by the operator')
            elif name == 'pixel':
                check(app.cmd('status')['data']['alarm_level'] == '未判级', 'startup has no normal vibration judgment')
                app.cmd('measure_start')
                last = app.result()
                check(last['valid'] and last['unit'] == 'px' and last['level'] == '未判级', 'uncalibrated pixels do not use mm alarm thresholds')
            elif name == 'laser':
                check(app.cmd('laser', on=True)['ok'], 'laser projector switches on')
                st = app.cmd('status')['data']
                check(st['laser']['on'] and not st['light']['on'], 'lighting and laser are separate outputs')
                app.cmd('measure_start')
                check(not app.cmd('status')['data']['laser']['on'], 'laser is off while displacement is tracked')
                check(not app.cmd('laser', on=True)['ok'], 'capture refuses manually re-enabling the projector')
                check(not app.cmd('status')['data']['laser']['on'], 'a refused projector command leaves LED2 off')
                check(not app.cmd('set', laser_off_during_measurement=False)['ok'],
                      'capture refuses changing the projector interlock policy')
                last = app.result()
                rec = json.loads((d / 'records' / last['measurement_id'] / 'record.json').read_text())
                check(rec['laser_projector'] == {'on': False, 'switched_off_for_capture': True},
                      'record notes the laser switched off for the capture')
                check(app.cmd('status')['data']['laser']['on'], 'laser is back on after the capture')
                check(app.cmd('mute')['ok'] and app.cmd('status')['data']['alarm_muted'], 'mute silences the alarm')
                check(app.cmd('mute', on=False)['ok'] and not app.cmd('status')['data']['alarm_muted'], 'mute with on=false undoes it')
            elif name == 'settings':
                app.cmd('zoom', steps=4)
                check(app.cmd('pan', x=0.99, y=0.01)['ok'], 'the zoomed view can be moved')
                m = app.cmd('status')['data']['measurement']
                h = 0.5 / m['zoom']
                check(m['zoom'] > 2 and abs(m['zoom_center_x'] - (1 - h)) < 1e-9 and abs(m['zoom_center_y'] - h) < 1e-9,
                      'the zoomed view stays inside the image')
                app.cmd('zoom_reset')
                app.cmd('roi', w=96, h=32)
                r = app.cmd('set', duration_s=30, warn_pp=0.5, alarm_pp=1.0)
                check(r['ok'] and r['data']['duration_s'] == 30, 'settings are applied')
                check(not app.cmd('set', warn_pp=2.0, alarm_pp=1.0)['ok'], 'warning threshold above the alarm threshold is refused')
                check(app.cmd('settings')['data']['warn_pp'] == 0.5, 'a refused change leaves the settings unchanged')
                saved = json.loads((d / 'settings.json').read_text())
                check(saved['measurement']['duration_s'] == 30 and saved['alarm']['alarm_peak_to_peak'] == 1.0,
                      'settings are written to the settings file')
                app.close()
                app = Daemon(d)
                s = app.ready() and app.cmd('settings')['data']
                check(s and s['duration_s'] == 30 and (s['roi_width_px'], s['roi_height_px']) == (96, 32),
                      'settings and the box size survive a restart')
            elif name == 'shutdown':
                app.cmd('measure_start', duration_s=3)
                time.sleep(.15)
                app.cmd('shutdown')
                time.sleep(2)
                check(app.cmd('status')['data']['shutdown_prepared'], 'shutdown completes during capture')
                check(not list((d / 'records').glob('*/record.json')), 'cancelled capture produces no late result')
        finally:
            app.close()
            check(app.p.returncode == 0, name + ': SIGTERM follows orderly exit')

    # Multi-point method: motor-off baseline, then the same layout with 7 Hz motion.
    d = root / 'multipoint'
    d.mkdir()
    mp = {'measurement': {'method': 'multipoint', 'duration_s': 4,
                          'multipoint': {'baseline_dir': str(d / 'baselines'), 'baseline_s': 5.5}}}
    app = Daemon(d, extra=[mp, {'camera': {'synthetic': {'fps': 60, 'amplitude_px': 0}}}])
    try:
        check(app.ready(), 'multipoint: starts')
        time.sleep(1.5)  # frame size known, baseline looked up
        st = app.cmd('status')['data']['measurement']['multipoint']
        check(not st['baseline_valid'] and st['layout_error'] == '', 'multipoint: valid layout, no baseline yet')
        r = app.cmd('measure_start')
        check(not r['ok'] and r['error'] == 'baseline_required', 'multipoint: measuring needs a baseline')
        check(app.cmd('baseline_start')['ok'], 'multipoint: baseline capture starts')
        for _ in range(300):
            st = app.cmd('status')['data']['measurement']['multipoint']
            if st['baseline_valid']:
                break
            time.sleep(.05)
        check(st['baseline_valid'] and st['baseline_windows'] >= 5, 'multipoint: baseline saved for this layout')
        check(len(list((d / 'baselines').glob('*.vbb'))) == 1, 'multipoint: one baseline file')
        check(not list((d / 'records').glob('*/record.json')), 'multipoint: a baseline is not a measurement record')
    finally:
        app.close()
    app = Daemon(d, extra=[mp, {'camera': {'synthetic': {'fps': 60, 'amplitude_px': 1.5, 'freq_hz': 7}}}])
    try:
        check(app.ready(), 'multipoint: restarts')
        time.sleep(1.5)
        check(app.cmd('status')['data']['measurement']['multipoint']['baseline_valid'],
              'multipoint: baseline found again after a restart')
        check(app.cmd('measure_start')['ok'], 'multipoint: measurement starts')
        last = app.result(polls=400)
        check(last['valid'] and abs(last['dominant_hz'] - 7) < 0.3, 'multipoint: 7 Hz found above the baseline')
        rec = json.loads((d / 'records' / last['measurement_id'] / 'record.json').read_text())
        res = rec['result']
        check(res['method'] == 'multipoint' and 0 < res['multipoint']['passed_points'] == len(res['multipoint']['points']),
              'multipoint: record lists the points above the baseline')
        check(res['unit'] == 'px' and res['level'] == 'unknown', 'multipoint: pixels, no mm alarm level')
        app.cmd('roi', x=0.45)
        time.sleep(1.5)
        check(not app.cmd('status')['data']['measurement']['multipoint']['baseline_valid'],
              'multipoint: moving the box needs a new baseline')
    finally:
        app.close()
        check(app.p.returncode == 0, 'multipoint: SIGTERM follows orderly exit')

    # Acoustic reference from the synthetic PDM source: a tone modulated at the
    # camera's 12 Hz; the record carries the analysis, its spectrum and the WAV.
    d = root / 'acoustic'
    d.mkdir()
    ac = {'acoustic': {'enabled': True, 'source': 'synthetic', 'synthetic_hz': 12}, 'measurement': {'duration_s': 3}}
    app = Daemon(d, extra=[ac, {'camera': {'synthetic': {'fps': 60, 'freq_hz': 12}}}])
    try:
        check(app.ready(), 'acoustic: starts')
        for _ in range(100):
            a = app.cmd('status')['data']['measurement']['acoustic']
            if a.get('state') == 'ready':
                break
            time.sleep(.05)
        check(a['state'] == 'ready' and a['source'] == 'synthetic' and abs(a['sample_rate_hz'] - 31250) < 1,
              'acoustic: synthetic source ready at 31.25 kS/s')
        check(app.cmd('measure_start')['ok'], 'acoustic: capture accepted')
        last = app.result(polls=300)
        al = last.get('acoustic') or {}
        check(last['valid'] and al.get('valid') and al.get('relation') == 'agree' and abs((al.get('frequency_hz') or 0) - 12) < 0.3,
              'acoustic: the envelope agrees with the camera at 12 Hz')
        rd = d / 'records' / last['measurement_id']
        rec = json.loads((rd / 'record.json').read_text())['acoustic']
        n = rec['window']['samples'] if rec['available'] else 0
        check(rec['valid'] and rec['source'] == 'synthetic' and abs(n - 3 * 31250) < 1100,  # the video window, within two frames
              'acoustic: record holds the capture window')
        check((rd / 'acoustic.wav').stat().st_size == 44 + 2 * n and (rd / 'acoustic_spectrum.csv').exists(),
              'acoustic: WAV and envelope spectrum saved')
    finally:
        app.close()
        check(app.p.returncode == 0, 'acoustic: SIGTERM follows orderly exit')

    # Phone app LAN API: discovery, pairing with the code shown on the device,
    # live state, commands, records with curves, raw files and the preview.
    import re
    import urllib.request
    import urllib.error
    d = root / 'appapi'
    d.mkdir()
    api = {'app_api': {'enabled': True, 'http_port': 0, 'discovery_port': 0, 'name': 'Line 3', 'require_pairing': True,
                       'tokens_file': str(d / 'tokens.json')}}
    app = Daemon(d, extra=[api, {'camera': {'synthetic': {'fps': 60, 'freq_hz': 9}}}])
    try:
        check(app.ready(), 'app api: starts')
        m = None
        for _ in range(100):
            m = re.search(r'app API on http port (\d+), discovery udp (\d+)', (d / 'daemon.log').read_text())
            if m:
                break
            time.sleep(.02)
        http, udp = int(m.group(1)), int(m.group(2))
        base = 'http://127.0.0.1:%d/api/v1/' % http

        def call(path, body=None, token=None, raw=False):
            req = urllib.request.Request(base + path, data=None if body is None else json.dumps(body).encode(),
                                         method='GET' if body is None else 'POST')
            if token:
                req.add_header('Authorization', 'Bearer ' + token)
            try:
                with urllib.request.urlopen(req, timeout=5) as r:
                    data = r.read()
                    return r.status, (data if raw else json.loads(data)), dict(r.headers)
            except urllib.error.HTTPError as e:
                return e.code, json.loads(e.read() or b'{}'), dict(e.headers)

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as u:
            u.settimeout(2)
            u.sendto(b'VIBMON_DISCOVER', ('127.0.0.1', udp))
            info = json.loads(u.recv(1024))
        check(info['service'] == 'vibmon' and info['kind'] == 'ax7010' and info['http_port'] == http and info['name'] == 'Line 3',
              'app api: UDP discovery answers with the HTTP port')
        st, body, hdr = call('live')
        check(st == 401 and hdr.get('Access-Control-Allow-Origin') == '*', 'app api: unpaired requests are refused (with CORS)')
        check(call('pair', {'pin': '000000', 'client': 'x'})[0] == 403, 'app api: pairing without a shown code fails')
        check(call('pair/request', {})[0] == 200, 'app api: pairing code requested')
        msg = app.cmd('status')['data']['message']
        pin = re.search(r'(\d{6})', msg)
        check(bool(pin) and '配对码' in msg, 'app api: the code is shown on the device screen')
        check(call('pair', {'pin': '999999' if pin.group(1) != '999999' else '111111'})[0] == 403, 'app api: a wrong code is refused')
        st, body, _ = call('pair', {'pin': pin.group(1), 'client': 'Pixel 9'})
        token = body.get('data', {}).get('token', '')
        check(st == 200 and len(token) == 32, 'app api: the right code returns a token')
        check(json.loads((d / 'tokens.json').read_text())['tokens'][0]['client'] == 'Pixel 9', 'app api: token kept on disk')
        st, body, _ = call('live', token=token)
        lv = body['data']
        check(st == 200 and lv['dev'] and lv['ms'] in ('preview', 'stopped') and lv['unit'] == 'mm' and isinstance(lv.get('wave'), list),
              'app api: live state with the recent waveform')
        check(len(json.dumps({k: v for k, v in lv.items() if k != 'wave'}, ensure_ascii=False).encode()) < 900,
              'app api: live state without the waveform fits a gateway frame')
        check(call('command', {'cmd': 'pan', 'x': 0.3}, token)[0] == 403, 'app api: commands outside the app list are refused')
        st, body, _ = call('command', {'cmd': 'set_point', 'id': 'P-07', 'name': '泵 7'}, token)
        check(st == 200 and body['ok'] and call('live', token=token)[1]['data']['pt']['id'] == 'P-07', 'app api: command applied')
        check(call('command', {'cmd': 'measure_start'}, token)[1]['ok'], 'app api: measurement started from the app')
        last = app.result(polls=300)
        lv = call('live', token=token)[1]['data']
        check(lv['last']['id'] == last['measurement_id'] and lv['last']['lv'] == 'normal' and lv['lv'] == 'normal',
              'app api: live state carries the result level')
        st, body, _ = call('records', token=token)
        recs = body['data']
        check(st == 200 and recs and recs[0]['id'] == last['measurement_id'] and recs[0]['pt'] == 'P-07' and abs(recs[0]['hz'] - 9) < 0.5
              and recs[0]['lv'] == 'normal',
              'app api: record list')
        st, body, _ = call('records/%s?points=200' % last['measurement_id'], token=token)
        det = body['data']
        check(st == 200 and det['record']['point_id'] == 'P-07' and 0 < len(det['waveform']['v']) <= 200 and
              len(det['spectrum']['f']) == len(det['spectrum']['a']) > 0, 'app api: record detail with decimated curves')
        sa = det['spectrum']['a']
        check(abs(det['spectrum']['f'][sa.index(max(sa))] - 9) < 0.5, 'app api: decimated spectrum keeps the 9 Hz peak')
        st, data, hdr = call('records/%s/files/waveform.csv' % last['measurement_id'], token=token, raw=True)
        check(st == 200 and data[:1].isalpha() and b',' in data.split(b'\n')[0], 'app api: raw CSV download')
        check(call('records/..%2Fcfg.json', token=token)[0] in (400, 404), 'app api: record ids cannot leave the records folder')
        st, data, hdr = call('preview.bmp?w=160&token=' + token, raw=True)
        check(st == 200 and data[:2] == b'BM' and struct.unpack_from('<i', data, 18)[0] <= 160, 'app api: camera preview as BMP')
        check(call('unpair', {}, token)[0] == 200 and call('live', token=token)[0] == 401, 'app api: unpair revokes the token')
    finally:
        app.close()
        check(app.p.returncode == 0, 'app api: SIGTERM follows orderly exit')
    # Default (private use): no pairing, every request is accepted.
    d = root / 'appopen'
    d.mkdir()
    app = Daemon(d, extra=[{'app_api': {'enabled': True, 'http_port': 0, 'discovery_port': -1}}])
    try:
        check(app.ready(), 'app api open: starts')
        for _ in range(100):
            m = re.search(r'app API on http port (\d+)', (d / 'daemon.log').read_text())
            if m:
                break
            time.sleep(.02)
        base = 'http://127.0.0.1:%s/api/v1/' % m.group(1)
        with urllib.request.urlopen(base + 'info', timeout=5) as r:
            info = json.loads(r.read())['data']
        with urllib.request.urlopen(base + 'live', timeout=5) as r:
            lv = json.loads(r.read())['data']
        check(info['pairing'] is False and lv['dev'] == info['device_id'], 'app api open: live state without pairing')
    finally:
        app.close()

    # Nano Wi-Fi gateway (protocol V1.1): a fake Nano on a pty announces
    # app_gateway, collects the PUBLISH topics and queues APP_* events.
    import os
    import pty
    import threading
    SOF, HDR = b'\xa5\x5a', struct.Struct('<BBBBHH')

    def crc16(data):
        crc = 0xffff
        for byte in data:
            crc ^= byte << 8
            for _ in range(8):
                crc = ((crc << 1) ^ 0x1021) & 0xffff if crc & 0x8000 else (crc << 1) & 0xffff
        return crc

    def frame(kind, op, seq, obj):
        body = json.dumps(obj, ensure_ascii=False, separators=(',', ':')).encode()
        data = HDR.pack(1, kind, op, 0, seq, len(body)) + body
        return SOF + data + struct.pack('<H', crc16(data))

    class FakeNano(threading.Thread):
        def __init__(self, fd):
            super().__init__(daemon=True)
            self.fd, self.topics, self.results, self.events, self.next_id = fd, {}, {}, [], 1
            self.lock, self.stop, self.publish_sizes = threading.Lock(), False, []

        def post(self, etype, data):
            with self.lock:
                ev = {'nano_boot_id': 'b00t', 'event_id': self.next_id, 'event_type': etype, 'uptime_ms': 1, 'data': data}
                self.events.append(ev)
                self.next_id += 1
                os.write(self.fd, frame(3, 0x40, 0, ev))

        def run(self):
            buf = bytearray()
            while not self.stop:
                try:
                    buf += os.read(self.fd, 4096)
                except OSError:
                    return
                while True:
                    i = buf.find(SOF)
                    if i < 0 or len(buf) < i + 10:
                        break
                    del buf[:i]
                    n = HDR.unpack(buf[2:10])[5]
                    if len(buf) < 12 + n:
                        break
                    _, kind, op, _, seq, _ = HDR.unpack(buf[2:10])
                    req = json.loads(buf[10:10 + n])
                    del buf[:12 + n]
                    sid = req.get('session_id')
                    data = {}
                    if op == 0x01:
                        data = {'nano_boot_id': 'b00t', 'nano_version': 'fake', 'protocol': 1, 'profile': 't',
                                'capabilities': ['nfc', 'wifi', 'app_gateway'],
                                'event_cursor': {'last_event_id': 0, 'acked_through': 0, 'pending': 0},
                                'link': {'baud': 115200, 'flow_control': False}}
                    elif op == 0x02:
                        data = {'wifi': {'state': 'CONNECTED', 'ssid': 'lab', 'ip': '10.1.2.3'}, 'sd': {'mounted': True},
                                'nfc': {'ready': True}, 'time_valid': False}
                    elif op == 0x11:
                        data = {'valid': False, 'source': 'none'}
                    elif op == 0x40:
                        with self.lock:
                            evs = [e for e in self.events if e['event_id'] > req['after_event_id']][:req['limit']]
                        data = {'events': evs, 'next_after': evs[-1]['event_id'] if evs else req['after_event_id'], 'more': False}
                    elif op == 0x41:
                        with self.lock:
                            self.events = [e for e in self.events if e['event_id'] > req['through_event_id']]
                        data = {'acked_through': req['through_event_id'], 'pending': len(self.events)}
                    elif op == 0x60:
                        self.publish_sizes.append(n)
                        self.topics[req['topic']] = req['data']
                    elif op == 0x62:
                        self.results[req['cid']] = req
                    os.write(self.fd, frame(2, op, seq, {'session_id': sid, 'ok': True, 'code': 'OK', 'data': data}))

    master, slave = pty.openpty()
    nano = FakeNano(master)
    nano.start()
    d = root / 'gateway'
    d.mkdir()
    gw = {'hardware': {'devices': {'uart_nano': os.ttyname(slave)}}, 'nano': {'flow_control': False},
          'app_api': {'enabled': True, 'http_port': 0, 'discovery_port': -1}}
    app = Daemon(d, extra=[gw, {'camera': {'synthetic': {'fps': 60, 'freq_hz': 7}}}])

    def wait(pred, polls=200):
        for _ in range(polls):
            if pred():
                return True
            time.sleep(.03)
        return False
    try:
        check(app.ready(), 'gateway: starts')
        check(wait(lambda: 'status' in nano.topics and 'settings' in nano.topics), 'gateway: status and settings published')
        check(wait(lambda: nano.topics['status']['nano']['wifi'] == 'CONNECTED') and nano.topics['status']['ms'] in ('preview', 'stopped'),
              'gateway: published status carries the live state')
        nano.post('APP_COMMAND', {'cid': 5, 'cmd': 'set_point', 'args': {'id': 'GW-1', 'name': '风机'}})
        check(wait(lambda: 5 in nano.results) and nano.results[5]['ok'], 'gateway: APP_COMMAND executed and answered')
        check(wait(lambda: nano.topics['status']['pt']['id'] == 'GW-1'), 'gateway: status republished after the command')
        nano.post('APP_COMMAND', {'cid': 6, 'cmd': 'roi', 'args': {'x': 0.1}})
        check(wait(lambda: 6 in nano.results) and nano.results[6]['error'] == 'command_not_allowed',
              'gateway: commands outside the gateway list are refused')
        nano.post('APP_CLIENT', {'client': '192.168.4.2'})
        check(wait(lambda: '192.168.4.2' in app.cmd('status')['data']['message']), 'gateway: a new phone is shown on the device screen')
        nano.post('APP_COMMAND', {'cid': 8, 'cmd': 'measure_start', 'args': {}})
        check(wait(lambda: 8 in nano.results) and nano.results[8]['ok'], 'gateway: measurement started through the gateway')
        last = app.result(polls=300)
        check(wait(lambda: nano.topics.get('curves', {}).get('id') == last['measurement_id'] and 'records' in nano.topics),
              'gateway: result curves and records published')
        c = nano.topics['curves']
        check(len(c['sf']) == len(c['sa']) >= 12 and abs(c['sf'][c['sa'].index(max(c['sa']))] - 7) < 0.5,
              'gateway: published spectrum keeps the 7 Hz peak')
        check(nano.topics['records']['items'][0]['id'] == last['measurement_id'], 'gateway: newest record first')
        check(max(nano.publish_sizes) <= 1024, 'gateway: every PUBLISH fits one frame')
    finally:
        app.close()
        nano.stop = True
        check(app.p.returncode == 0, 'gateway: SIGTERM follows orderly exit')

sys.exit(bool(failures))
