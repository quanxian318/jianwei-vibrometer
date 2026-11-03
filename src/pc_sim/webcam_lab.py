"""见微 PC 视觉实验台 - live webcam test bench for the vision algorithms.

    python webcam_lab.py [--camera "Integrated Camera"] [--size 640x480] [--fps 30]

Views (all from the bit-exact model in vision_model.py, i.e. what the PL
computes): motion magnification, band energy heat map, per-pixel brightness
time series, dominant-frequency map. Click the image to place the ROI: the
ROI is tracked with the same NCC search as the PS (displacement waveform and
spectrum below). "录制片段" stores the next frames as a VIS1 clip for
check_bitexact.py / the PL testbench.
"""
import argparse
import collections
import pathlib
import sys
import tempfile
import threading
import time
import tkinter as tk
from tkinter import ttk

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageTk

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import camera  # noqa: E402
import vision_model as vm  # noqa: E402

PANEL_W, PANEL_H = 480, 360
PLOT_W, PLOT_H = 480, 170


def turbo_lut():
    t = np.linspace(0, 1, 256)
    r = 0.13572138 + t * (4.61539260 + t * (-42.66032258 + t * (132.13108234 + t * (-152.94239396 + t * 59.28637943))))
    g = 0.09140261 + t * (2.19418839 + t * (4.84296658 + t * (-14.18503333 + t * (4.27729857 + t * 2.82956604))))
    b = 0.10667330 + t * (12.64194608 + t * (-60.58204836 + t * (110.36276771 + t * (-89.90310912 + t * 27.34824973))))
    return (np.clip(np.stack([r, g, b], 1), 0, 1) * 255).astype(np.uint8)


TURBO = turbo_lut()


def find_font(size):
    for f in (r'C:\Windows\Fonts\Noto Sans SC Medium (TrueType).otf', r'C:\Windows\Fonts\msyh.ttc',
              r'C:\Windows\Fonts\simhei.ttf'):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            continue
    return ImageFont.load_default()


FONT = find_font(15)


class NccTracker:
    """Same search as src/measure/tracker.cpp: coarse step-2 grid, 5x5 refine, parabolic sub-pixel."""

    def __init__(self, ref, x, y, size, radius=8):
        self.size, self.radius = size, radius
        self.x = int(np.clip(x - size // 2, radius + 3, ref.shape[1] - size - radius - 3))
        self.y = int(np.clip(y - size // 2, radius + 3, ref.shape[0] - size - radius - 3))
        t = ref[self.y:self.y + size, self.x:self.x + size].astype(np.float64)
        self.t = t - t.mean()
        self.tnorm = np.sqrt((self.t ** 2).sum())
        self.cx = self.cy = 0
        self.ok = self.t.std() >= 2.0

    def score(self, f, u, v):
        x0, y0 = self.x + u, self.y + v
        if x0 < 0 or y0 < 0 or x0 + self.size > f.shape[1] or y0 + self.size > f.shape[0]:
            return -1.5
        p = f[y0:y0 + self.size, x0:x0 + self.size].astype(np.float64)
        n = p.size
        var = (p ** 2).sum() - p.sum() ** 2 / n
        return float((p * self.t).sum() / (self.tnorm * np.sqrt(var))) if var > 1e-6 else 0.0

    def track(self, f):
        r = self.radius
        cache = {}

        def s(u, v):
            if (u, v) not in cache:
                cache[(u, v)] = self.score(f, u, v)
            return cache[(u, v)]
        best, bu, bv = -3.0, self.cx, self.cy
        for v in range(self.cy - r, self.cy + r + 1, 2):
            for u in range(self.cx - r, self.cx + r + 1, 2):
                if s(u, v) > best:
                    best, bu, bv = s(u, v), u, v
        cu, cv = bu, bv
        for v in range(cv - 2, cv + 3):
            for u in range(cu - 2, cu + 3):
                if abs(u - self.cx) > r + 1 or abs(v - self.cy) > r + 1:
                    continue
                if s(u, v) > best:
                    best, bu, bv = s(u, v), u, v

        def refine(m, z, p):
            d = m - 2 * z + p
            return 0.0 if m < -1 or p < -1 or d >= 0 else float(np.clip(0.5 * (m - p) / d, -0.5, 0.5))
        dx = bu + refine(s(bu - 1, bv), best, s(bu + 1, bv))
        dy = bv + refine(s(bu, bv - 1), best, s(bu, bv + 1))
        self.cx, self.cy = bu, bv
        return dx, dy, best


def spectrum(x, fs):
    x = np.asarray(x, np.float64)
    if len(x) < 16 or fs <= 0:
        return np.array([]), np.array([])
    t = np.arange(len(x))
    x = x - np.polyval(np.polyfit(t, x, 1), t)
    w = np.hanning(len(x))
    a = np.abs(np.fft.rfft(x * w)) * 2 / w.sum()
    return np.fft.rfftfreq(len(x), 1 / fs), a


def plot(values_x, values_y, title, unit, color=(80, 220, 120), marker=None, from_zero=False):
    img = Image.new('RGB', (PLOT_W, PLOT_H), (21, 30, 41))
    d = ImageDraw.Draw(img)
    d.text((10, 6), title, fill=(160, 172, 185), font=FONT)
    x0, y0, x1, y1 = 46, 30, PLOT_W - 10, PLOT_H - 22
    d.rectangle((x0, y0, x1, y1), outline=(40, 52, 66))
    if len(values_x) < 2:
        d.text((x0 + 120, y0 + 50), '等待数据', fill=(110, 120, 130), font=FONT)
        return img
    xs, ys = np.asarray(values_x, float), np.asarray(values_y, float)
    lo, hi = (0.0 if from_zero else ys.min()), ys.max()
    if hi - lo < 1e-9:
        hi, lo = hi + 1, lo - 1
    pad = (hi - lo) * 0.08
    hi += pad
    lo = lo if from_zero else lo - pad
    px = x0 + (xs - xs[0]) / max(1e-9, xs[-1] - xs[0]) * (x1 - x0)
    py = y1 - (ys - lo) / (hi - lo) * (y1 - y0)
    d.line(list(zip(px.tolist(), py.tolist())), fill=color, width=2)
    d.text((4, y0 - 2), '%.3g' % hi, fill=(130, 140, 150), font=FONT)
    d.text((4, y1 - 14), '%.3g' % lo, fill=(130, 140, 150), font=FONT)
    d.text((x0, y1 + 3), '%.3g' % xs[0], fill=(130, 140, 150), font=FONT)
    d.text((x1 - 60, y1 + 3), '%.3g %s' % (xs[-1], unit), fill=(130, 140, 150), font=FONT)
    if marker is not None and xs[0] <= marker[0] <= xs[-1]:
        mx = x0 + (marker[0] - xs[0]) / max(1e-9, xs[-1] - xs[0]) * (x1 - x0)
        d.line((mx, y0, mx, y1), fill=(245, 165, 36))
        d.text((min(mx + 4, x1 - 150), y0 + 2), marker[1], fill=(245, 165, 36), font=FONT)
    return img


class Lab:
    VIEWS = ['运动放大', '振动能量热力', '主频分布', '原图']

    def __init__(self, root, args):
        self.root, self.args = root, args
        root.title('见微 PC 视觉实验台（位精确模型 = PL 算法）')
        root.configure(bg='#0A0F15')
        self.cam = camera.Camera(args.camera, args.width, args.height, args.fps)
        self.cam.start()
        self.lock = threading.Lock()
        self.running = True
        self.state = None
        self.out = None
        self.fps = float(args.fps)
        self.times = collections.deque(maxlen=60)
        self.proc_ms = 0.0
        self.history = collections.deque(maxlen=256)  # band-level (L1) frames for the frequency map
        self.g0_hist = collections.deque(maxlen=512)  # probe pixel brightness
        self.probe = None
        self.tracker = None
        self.disp = collections.deque(maxlen=600)
        self.disp_t = collections.deque(maxlen=600)
        self.record_left = 0
        self.record_frames = []
        self.freq_map = None
        self.last_freq = 0.0
        self.frame = None
        self._build()
        threading.Thread(target=self._worker, daemon=True).start()
        self._refresh()

    # ------------------------------------------------------------------ UI
    def _build(self):
        style = ttk.Style()
        style.theme_use('clam')
        bar = tk.Frame(self.root, bg='#101822')
        bar.pack(fill='x')
        self.view = tk.StringVar(value=self.VIEWS[0])
        for v in self.VIEWS:
            tk.Radiobutton(bar, text=v, variable=self.view, value=v, indicatoron=0, bg='#26364A', fg='white',
                           selectcolor='#3AA0FF', padx=10, pady=4, font=('Microsoft YaHei', 10)).pack(side='left',
                                                                                                    padx=3, pady=4)
        tk.Label(bar, text='  频带 Hz', bg='#101822', fg='#98A6B5').pack(side='left')
        self.low = tk.DoubleVar(value=self.args.low)
        self.high = tk.DoubleVar(value=self.args.high)
        tk.Spinbox(bar, from_=0.2, to=30, increment=0.2, width=5, textvariable=self.low).pack(side='left')
        tk.Label(bar, text='–', bg='#101822', fg='#98A6B5').pack(side='left')
        tk.Spinbox(bar, from_=0.4, to=40, increment=0.5, width=5, textvariable=self.high).pack(side='left')
        tk.Label(bar, text='  放大', bg='#101822', fg='#98A6B5').pack(side='left')
        self.gain = tk.IntVar(value=self.args.gain)
        tk.Spinbox(bar, values=(5, 10, 20, 30, 50, 80), width=4, textvariable=self.gain).pack(side='left')
        tk.Button(bar, text='录制 90 帧', command=self._record, bg='#26364A', fg='white').pack(side='left', padx=8)
        tk.Button(bar, text='清除测点', command=self._clear, bg='#26364A', fg='white').pack(side='left')
        self.status = tk.Label(bar, text='', bg='#101822', fg='#E8EEF4', font=('Consolas', 10))
        self.status.pack(side='right', padx=8)

        grid = tk.Frame(self.root, bg='#0A0F15')
        grid.pack()
        self.left = tk.Label(grid, bg='black')
        self.left.grid(row=0, column=0, padx=4, pady=4)
        self.right = tk.Label(grid, bg='black')
        self.right.grid(row=0, column=1, padx=4, pady=4)
        self.plot1 = tk.Label(grid, bg='#151E29')
        self.plot1.grid(row=1, column=0, padx=4, pady=2)
        self.plot2 = tk.Label(grid, bg='#151E29')
        self.plot2.grid(row=1, column=1, padx=4, pady=2)
        self.plot3 = tk.Label(grid, bg='#151E29')
        self.plot3.grid(row=2, column=0, padx=4, pady=2)
        self.plot4 = tk.Label(grid, bg='#151E29')
        self.plot4.grid(row=2, column=1, padx=4, pady=2)
        self.left.bind('<Button-1>', self._click_track)
        self.right.bind('<Button-1>', self._click_probe)
        tk.Label(self.root, text='左图点击：放置测点框并跟踪位移（与 PS 端 NCC 算法相同）    右图点击：查看该点亮度时序',
                 bg='#0A0F15', fg='#98A6B5', font=('Microsoft YaHei', 10)).pack(pady=2)
        self.root.protocol('WM_DELETE_WINDOW', self._close)

    def _click_track(self, ev):
        f = self.frame
        if f is None:
            return
        x = int(ev.x * f.shape[1] / PANEL_W)
        y = int(ev.y * f.shape[0] / PANEL_H)
        t = NccTracker(f, x, y, self.args.roi)
        if not t.ok:
            self.status.config(text='测点纹理不足，换一个位置')
            return
        with self.lock:
            self.tracker = t
            self.disp.clear()
            self.disp_t.clear()

    def _click_probe(self, ev):
        with self.lock:
            self.probe = (ev.x / PANEL_W, ev.y / PANEL_H)
            self.g0_hist.clear()

    def _clear(self):
        with self.lock:
            self.tracker = None
            self.probe = None

    def _record(self):
        with self.lock:
            self.record_frames = []
            self.record_left = 90

    def _close(self):
        self.running = False
        self.cam.stop()
        self.root.destroy()

    # -------------------------------------------------------------- worker
    def _worker(self):
        seq = 0
        last_t = None
        while self.running:
            got = self.cam.wait(seq, timeout=1.0)
            if got is None:
                continue
            gray, t, seq = got
            if last_t is not None and t > last_t:
                self.times.append(t - last_t)
                self.fps = 1.0 / np.median(self.times)
            last_t = t
            low, high = max(0.1, self.low.get()), max(0.2, self.high.get())
            fs = self.fps
            ch = vm.lowpass_coef(min(high, fs * 0.45), fs)
            cl = vm.lowpass_coef(min(low, fs * 0.45), fs)
            t0 = time.perf_counter()
            out = vm.core(gray, 0, ch, cl, int(self.gain.get()), self.state)
            self.state = out.state
            self.proc_ms = (time.perf_counter() - t0) * 1000
            with self.lock:
                self.frame = gray
                self.out = out
                self.history.append(out.lap.astype(np.float32))
                if self.probe is not None:
                    py = int(self.probe[1] * out.g0.shape[0])
                    px = int(self.probe[0] * out.g0.shape[1])
                    self.g0_hist.append((t, float(out.g0[min(py, out.g0.shape[0] - 1), min(px, out.g0.shape[1] - 1)])))
                if self.tracker is not None:
                    dx, dy, score = self.tracker.track(gray)
                    self.disp.append(dy if self.args.axis == 'y' else dx)
                    self.disp_t.append(t)
                if self.record_left > 0:
                    self.record_frames.append(gray.copy())
                    self.record_left -= 1
                    if self.record_left == 0:
                        self._save_clip(list(self.record_frames), ch, cl)

    def _save_clip(self, frames, ch, cl):
        out = pathlib.Path(tempfile.gettempdir()) / 'vibmon_pcsim'
        out.mkdir(parents=True, exist_ok=True)
        path = out / ('clip_%s.vis' % time.strftime('%H%M%S'))
        vm.write_vis(path, frames, 0, ch, cl, int(self.gain.get()))
        self.root.after(0, lambda: self.status.config(text='已录制 %s' % path))

    def _frequency_map(self, fs):
        """Dominant frequency per band pixel from the last frames (brightness time series)."""
        if len(self.history) < 64:
            return None
        cube = np.stack(list(self.history)[-128:])
        cube = cube - cube.mean(axis=0)
        spec = np.abs(np.fft.rfft(cube * np.hanning(len(cube))[:, None, None], axis=0))
        freqs = np.fft.rfftfreq(len(cube), 1 / fs)
        spec[0] = 0
        k = spec.argmax(axis=0)
        peak = spec.max(axis=0)
        mask = peak > max(3 * np.median(peak), 1e-6)
        return freqs[k], mask, freqs[-1]

    # ------------------------------------------------------------- refresh
    def _refresh(self):
        if not self.running:
            return
        with self.lock:
            out, frame = self.out, self.frame
            disp, disp_t = list(self.disp), list(self.disp_t)
            g0h = list(self.g0_hist)
            tracker = self.tracker
            probe = self.probe
        if out is not None:
            view = self.view.get()
            left = Image.fromarray(frame).convert('RGB').resize((PANEL_W, PANEL_H), Image.BILINEAR)
            dl = ImageDraw.Draw(left)
            if tracker is not None:
                sx, sy = PANEL_W / frame.shape[1], PANEL_H / frame.shape[0]
                x0 = (tracker.x + tracker.cx) * sx
                y0 = (tracker.y + tracker.cy) * sy
                dl.rectangle((x0, y0, x0 + tracker.size * sx, y0 + tracker.size * sy), outline=(245, 165, 36), width=2)
            dl.text((8, 6), '原图 %dx%d' % (frame.shape[1], frame.shape[0]), fill=(255, 255, 255), font=FONT)
            g0 = out.g0
            if view == '运动放大':
                right = Image.fromarray(out.mag).convert('RGB')
                label = '运动放大 ×%d（%s）' % (self.gain.get(), '位精确')
            elif view == '振动能量热力':
                e = vm.energy_display(out.amp)
                heat = TURBO[np.kron(e, np.ones((2, 2), np.uint8))[:g0.shape[0], :g0.shape[1]]]
                base = np.repeat(g0[:, :, None], 3, axis=2).astype(np.float32)
                right = Image.fromarray((base * 0.45 + heat * 0.55).astype(np.uint8))
                label = '带通幅值（亮度时序能量）'
            elif view == '主频分布':
                fm = self._frequency_map(self.fps)
                if fm is None:
                    right = Image.fromarray(g0).convert('RGB')
                    label = '主频分布：采集中 %d/64 帧' % len(self.history)
                else:
                    f, mask, fmax = fm
                    idx = np.clip(f / max(fmax, 1e-6) * 255, 0, 255).astype(np.uint8)
                    col = TURBO[idx]
                    col[~mask] = (25, 30, 36)
                    col = np.kron(col, np.ones((2, 2, 1), np.uint8))[:g0.shape[0], :g0.shape[1]]
                    base = np.repeat(g0[:, :, None], 3, axis=2).astype(np.float32)
                    right = Image.fromarray((base * 0.35 + col * 0.65).astype(np.uint8))
                    label = '每像素主频 0–%.1f Hz（蓝低红高，暗处无明显峰）' % fmax
            else:
                right = Image.fromarray(g0).convert('RGB')
                label = '工作图 G0 %dx%d' % (g0.shape[1], g0.shape[0])
            right = right.resize((PANEL_W, PANEL_H), Image.BILINEAR)
            dr = ImageDraw.Draw(right)
            dr.text((8, 6), label, fill=(255, 255, 255), font=FONT)
            if probe is not None:
                px, py = probe[0] * PANEL_W, probe[1] * PANEL_H
                dr.ellipse((px - 8, py - 8, px + 8, py + 8), outline=(245, 165, 36), width=2)
            self._show(self.left, left)
            self._show(self.right, right)
            # Plots.
            if disp_t:
                t0 = disp_t[0]
                p1 = plot([t - t0 for t in disp_t], disp, '测点位移（px，NCC 跟踪）', 's')
                f, a = spectrum(disp, self.fps)
                mk = None
                if len(a) > 2:
                    k = int(np.argmax(a[1:]) + 1)
                    self.last_freq = f[k]
                    mk = (f[k], '%.2f Hz  %.3f px' % (f[k], a[k]))
                p2 = plot(f, a, '位移幅值谱', 'Hz', color=(58, 160, 255), marker=mk, from_zero=True)
            else:
                p1 = plot([], [], '测点位移：在左图点击放置测点框', 's')
                p2 = plot([], [], '位移幅值谱', 'Hz')
            if g0h:
                t0 = g0h[0][0]
                p3 = plot([t - t0 for t, _ in g0h], [v for _, v in g0h], '探针像素亮度时序（灰度）', 's',
                          color=(155, 123, 255))
                f, a = spectrum([v for _, v in g0h], self.fps)
                mk = None
                if len(a) > 2:
                    k = int(np.argmax(a[1:]) + 1)
                    mk = (f[k], '%.2f Hz' % f[k])
                p4 = plot(f, a, '探针像素亮度谱', 'Hz', color=(155, 123, 255), marker=mk, from_zero=True)
            else:
                p3 = plot([], [], '亮度时序：在右图点击选择像素', 's')
                p4 = plot([], [], '亮度谱', 'Hz')
            for w, im in ((self.plot1, p1), (self.plot2, p2), (self.plot3, p3), (self.plot4, p4)):
                self._show(w, im)
            rec = ('  录制中 %d' % self.record_left) if self.record_left else ''
            self.status.config(text='%.1f fps  模型 %.1f ms/帧  G0 %dx%d  L1 %dx%d%s' % (
                self.fps, self.proc_ms, g0.shape[1], g0.shape[0], out.lap.shape[1], out.lap.shape[0], rec))
        self.root.after(50, self._refresh)

    def selftest(self, seconds):
        out = pathlib.Path(tempfile.gettempdir()) / 'vibmon_pcsim'
        out.mkdir(parents=True, exist_ok=True)
        start = time.monotonic()

        class Ev:
            x, y = PANEL_W // 2, PANEL_H // 2

        def step():
            el = time.monotonic() - start
            if self.frame is not None and self.tracker is None and el > 1.0:
                self._click_track(Ev())
                self._click_probe(Ev())
            self.view.set(self.VIEWS[min(3, int(el / seconds * 4))])
            for name in ('left', 'right', 'plot1', 'plot2', 'plot3', 'plot4'):
                w = getattr(self, name)
                if getattr(w, 'image', None) is not None:
                    ImageTk.getimage(w.image).save(out / ('lab_%s_%d.png' % (name, int(el / seconds * 4))))
            if el >= seconds:
                print('selftest: %.1f fps, model %.1f ms/frame, %d tracked samples, panels in %s' % (
                    self.fps, self.proc_ms, len(self.disp), out))
                self._close()
                return
            self.root.after(500, step)
        self.root.after(500, step)

    @staticmethod
    def _show(widget, img):
        tkimg = ImageTk.PhotoImage(img)
        widget.configure(image=tkimg)
        widget.image = tkimg


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--camera', default='Integrated Camera')
    ap.add_argument('--size', default='640x480')
    ap.add_argument('--fps', type=int, default=30)
    ap.add_argument('--low', type=float, default=1.0)
    ap.add_argument('--high', type=float, default=5.0)
    ap.add_argument('--gain', type=int, default=20)
    ap.add_argument('--roi', type=int, default=64)
    ap.add_argument('--axis', choices=('x', 'y'), default='y')
    ap.add_argument('--selftest', type=float, default=0,
                    help='run N seconds, cycle the views, save the panels to %%TEMP%%\vibmon_pcsim and exit')
    args = ap.parse_args()
    args.width, args.height = (int(v) for v in args.size.lower().split('x'))
    root = tk.Tk()
    lab = Lab(root, args)
    if args.selftest > 0:
        lab.selftest(args.selftest)
    root.mainloop()


if __name__ == '__main__':
    main()
