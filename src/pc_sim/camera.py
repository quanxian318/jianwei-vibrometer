"""PC camera capture (DirectShow through PyAV) delivering 8-bit gray frames.

The webcam's YUYV 4:2:2 output is the same pixel format as the OV5640 path
of the AN5642 base design, so the Y channel used here is what the PL sees.
"""
import threading
import time

import numpy as np


def list_cameras():
    """Names of DirectShow video devices (empty list when none or no PyAV)."""
    try:
        import av
        import av.logging
    except ImportError:
        return []
    names = []
    try:
        av.logging.set_level(av.logging.PANIC)
        av.open('dummy', format='dshow', options={'list_devices': 'true'})
    except Exception as e:  # PyAV reports the device list through the error log
        text = str(e)
        names = [t.split('"')[1] for t in text.splitlines() if '"' in t]
    return names


class Camera:
    """Background capture thread; latest() returns (gray, t_s, seq) of the newest frame."""

    def __init__(self, name='Integrated Camera', width=640, height=480, fps=30):
        import av
        self._av = av
        self.name, self.width, self.height, self.fps = name, width, height, fps
        self._lock = threading.Lock()
        self._cond = threading.Condition(self._lock)
        self._frame = None
        self._seq = 0
        self._t = 0.0
        self._running = False
        self._thread = None
        self.error = ''

    def start(self):
        opts = {'video_size': '%dx%d' % (self.width, self.height), 'framerate': str(self.fps), 'rtbufsize': '64M'}
        self._container = self._av.open('video=' + self.name, format='dshow', options=opts)
        self._running = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        stream = self._container.streams.video[0]
        try:
            for frame in self._container.decode(stream):
                if not self._running:
                    break
                gray = frame.to_ndarray(format='gray')
                # Device timestamp (100 ns DirectShow clock) when available, else host time.
                t = float(frame.pts * frame.time_base) if frame.pts is not None else time.monotonic()
                with self._cond:
                    self._frame = gray
                    self._t = t
                    self._seq += 1
                    self._cond.notify_all()
        except Exception as e:  # device unplugged etc.
            self.error = str(e)
        finally:
            self._running = False

    def wait(self, last_seq, timeout=1.0):
        """Blocks until a frame newer than last_seq exists; returns (gray, t, seq) or None."""
        with self._cond:
            if not self._cond.wait_for(lambda: self._seq != last_seq or not self._running, timeout):
                return None
            if self._frame is None:
                return None
            return self._frame, self._t, self._seq

    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=2)
        try:
            self._container.close()
        except Exception:
            pass


def record(n, name='Integrated Camera', width=640, height=480, fps=30, warmup=15):
    """Returns n consecutive gray frames and their timestamps (after auto-exposure warm-up)."""
    cam = Camera(name, width, height, fps)
    cam.start()
    frames, times, seq = [], [], 0
    try:
        while len(frames) < n + warmup:
            got = cam.wait(seq, timeout=3)
            if got is None:
                raise RuntimeError('camera delivered no frames: ' + cam.error)
            gray, t, s = got
            if s != seq + 1 and seq:
                frames, times = [], []  # keep the sequence gap-free
            seq = s
            frames.append(gray.copy())
            times.append(t)
    finally:
        cam.stop()
    return np.stack(frames[warmup:]), np.array(times[warmup:])
