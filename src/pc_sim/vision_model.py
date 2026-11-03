"""Bit-exact integer model of the vision core.

Same arithmetic as ax7010/linux/vibmon/src/measure/vision.cpp (PS reference)
and ax7010/pl/vision (PL). Every intermediate value is an integer; shifts are
arithmetic (floor) as in C++ and Verilog.

    camera gray --REDUCE x levels--> G0
    G1 = REDUCE(G0), G2 = REDUCE(G1), L1 = G1 - EXPAND(G2)
    per L1 pixel: two first-order low-passes (Q6 int16 states), band = lpH - lpL
    magnified = clamp(G0 + EXPAND(clamp((gain * band + 32) >> 6, +-96)), 0, 255)
    amp      += (|band| - amp) >> 4          (running mean of |band|, Q6)
"""
import math

import numpy as np

STATE_FRAC = 6
DELTA_LIMIT = 96
ENERGY_SHIFT = 4


def _clamped(idx, n):
    return np.clip(idx, 0, n - 1)


def pyr_reduce(img):
    """[1 4 6 4 1]/16 per axis with edge replicate, keep even rows/columns."""
    img = img.astype(np.int32)
    h, w = img.shape
    ow, oh = (w + 1) // 2, (h + 1) // 2
    xs = 2 * np.arange(ow)
    t = (img[:, _clamped(xs - 2, w)] + 4 * img[:, _clamped(xs - 1, w)] + 6 * img[:, xs] +
         4 * img[:, _clamped(xs + 1, w)] + img[:, _clamped(xs + 2, w)])
    ys = 2 * np.arange(oh)
    v = (t[_clamped(ys - 2, h)] + 4 * t[_clamped(ys - 1, h)] + 6 * t[ys] +
         4 * t[_clamped(ys + 1, h)] + t[_clamped(ys + 2, h)])
    return ((v + 128) >> 8).astype(np.uint8)


def _expand_axis(a, n_out, axis):
    n = a.shape[axis]
    x = np.arange(n_out)
    i = x >> 1
    take = lambda idx: np.take(a, _clamped(idx, n), axis=axis)
    even = take(i - 1) + 6 * take(i) + take(i + 1)
    odd = 4 * (take(i) + take(i + 1))
    shape = [1, 1]
    shape[axis] = n_out
    mask = ((x & 1) == 0).reshape(shape)
    return np.where(mask, even, odd)


def pyr_expand(a, ow, oh):
    """2x up-sampling with the matching kernel; (sum + 32) >> 6, saturated to int16."""
    a = a.astype(np.int32)
    t = _expand_axis(a, ow, axis=1)
    v = _expand_axis(t, oh, axis=0)
    return np.clip((v + 32) >> 6, -32768, 32767).astype(np.int16)


def lowpass_coef(f_hz, fs):
    """Q16 coefficient of a first-order low-pass (C++ std::lround for positive values)."""
    if fs <= 0:
        return 1
    r = 1.0 - math.exp(-2.0 * math.pi * f_hz / fs)
    return max(1, min(65535, int(math.floor(r * 65536.0 + 0.5))))


def working_levels(w, levels):
    if levels > 0:
        return levels
    n = 0
    while w > 200:
        w = (w + 1) // 2
        n += 1
    return n


def _sat16(v):
    return np.clip(v, -32768, 32767)


class BandState:
    def __init__(self, shape):
        self.lp_h = np.zeros(shape, np.int16)
        self.lp_l = np.zeros(shape, np.int16)
        self.amp = np.zeros(shape, np.uint16)


def band_step(lap, coef_h, coef_l, st):
    """One temporal step; updates st, returns the band (int16)."""
    x = lap.astype(np.int64) << STATE_FRAC
    h = st.lp_h.astype(np.int64)
    l = st.lp_l.astype(np.int64)
    h = h + ((coef_h * (x - h) + 32768) >> 16)
    l = l + ((coef_l * (x - l) + 32768) >> 16)
    st.lp_h = _sat16(h).astype(np.int16)
    st.lp_l = _sat16(l).astype(np.int16)
    b = _sat16(st.lp_h.astype(np.int32) - st.lp_l.astype(np.int32)).astype(np.int16)
    a = st.amp.astype(np.int32)
    st.amp = (a + ((np.abs(b.astype(np.int32)) - a) >> ENERGY_SHIFT)).astype(np.uint16)
    return b


class CoreOut:
    pass


def core(gray, levels, coef_h, coef_l, gain, st):
    """Complete per-frame computation (what the PL implements). st: BandState or None."""
    g0 = np.asarray(gray, np.uint8)
    for _ in range(working_levels(g0.shape[1], levels)):
        g0 = pyr_reduce(g0)
    g1 = pyr_reduce(g0)
    g2 = pyr_reduce(g1)
    lap = (g1.astype(np.int32) - pyr_expand(g2, g1.shape[1], g1.shape[0]).astype(np.int32)).astype(np.int16)
    if st is None or st.lp_h.shape != lap.shape:
        st = BandState(lap.shape)
    band = band_step(lap, coef_h, coef_l, st)
    delta = np.clip((gain * band.astype(np.int32) + 32) >> STATE_FRAC, -DELTA_LIMIT, DELTA_LIMIT).astype(np.int16)
    up = pyr_expand(delta, g0.shape[1], g0.shape[0])
    out = CoreOut()
    out.g0 = g0
    out.lap = lap
    out.band = band
    out.mag = np.clip(g0.astype(np.int32) + up, 0, 255).astype(np.uint8)
    out.amp = st.amp.copy()
    out.state = st
    return out


def energy_display(amp):
    """PS display scaling: frame maximum, at least 2 gray levels full scale."""
    full = max(int(amp.max()), 2 << STATE_FRAC)
    return np.minimum(255, amp.astype(np.int64) * 255 // full).astype(np.uint8)


# ---------------------------------------------------------------------------
# Recording format shared with vision_ref (C++) and the PL testbench.
# ---------------------------------------------------------------------------

def write_vis(path, frames, levels, coef_h, coef_l, gain):
    frames = [np.asarray(f, np.uint8) for f in frames]
    h, w = frames[0].shape
    with open(path, 'wb') as f:
        f.write(b'VIS1')
        f.write(np.array([w, h, len(frames)], '<u2').tobytes())
        f.write(bytes([levels, 0]))
        f.write(np.array([coef_h, coef_l, gain], '<u2').tobytes())
        for fr in frames:
            f.write(fr.tobytes())


def read_vis(path):
    data = open(path, 'rb').read()
    assert data[:4] == b'VIS1', 'not a VIS1 file'
    w, h, n = np.frombuffer(data, '<u2', 3, 4)
    levels = data[10]
    coef_h, coef_l, gain = np.frombuffer(data, '<u2', 3, 12)
    frames = np.frombuffer(data, np.uint8, int(w) * int(h) * int(n), 18).reshape(int(n), int(h), int(w))
    return frames, int(levels), int(coef_h), int(coef_l), int(gain)


def read_vout(path):
    data = open(path, 'rb').read()
    assert data[:4] == b'VOUT', 'not a VOUT file'
    w0, h0, w1, h1, n = (int(v) for v in np.frombuffer(data, '<u2', 5, 4))
    pos, out = 14, []
    for _ in range(n):
        g0 = np.frombuffer(data, np.uint8, w0 * h0, pos).reshape(h0, w0)
        pos += w0 * h0
        mag = np.frombuffer(data, np.uint8, w0 * h0, pos).reshape(h0, w0)
        pos += w0 * h0
        band = np.frombuffer(data, '<i2', w1 * h1, pos).reshape(h1, w1)
        pos += 2 * w1 * h1
        amp = np.frombuffer(data, '<u2', w1 * h1, pos).reshape(h1, w1)
        pos += 2 * w1 * h1
        out.append((g0, mag, band, amp))
    return out
