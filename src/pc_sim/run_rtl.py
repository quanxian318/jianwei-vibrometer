"""Runs the PL vision RTL testbenches with Icarus Verilog in WSL and compares
their outputs with the bit-exact Python model.

    python run_rtl.py reduce [--synthetic]   # vp_reduce alone, even and odd heights
    python run_rtl.py core [--synthetic]     # reduce chain + vp_core, magnified and energy

Uses webcam frames unless --synthetic is given. Work files go to
%TEMP%\\vibmon_pcsim\\rtl (removed after a passing run unless --keep).
"""
import argparse
import pathlib
import shutil
import sys
import tempfile

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import vision_model as vm  # noqa: E402
from check_bitexact import synthetic, wsl, wsl_path  # noqa: E402

PL = HERE.parent / 'ax7010' / 'pl' / 'vision'


def write_hex(path, data):
    data = np.asarray(data).reshape(-1)
    path.write_text('\n'.join('%02x' % int(v) for v in data) + '\n')


def read_hex(path, signed16=False):
    vals = [int(t, 16) for t in path.read_text().split()]
    a = np.array(vals, np.int64)
    if signed16:
        a = np.where(a >= 32768, a - 65536, a)
    return a


def iverilog(work, top, sources, params, timeout=1800):
    defs = ' '.join('-P%s.%s=%s' % (top, k, v) for k, v in params.items())
    srcs = ' '.join('"%s"' % wsl_path(s) for s in sources)
    cmd = 'cd "%s" && iverilog -g2005 -Wall -s %s %s -o sim.vvp %s 2>&1 | grep -v "sensitive to all" ; vvp -n sim.vvp' % (
        wsl_path(work), top, defs, srcs)
    return wsl(cmd, timeout)


def reduce_test(frames, work):
    ok = True
    for h_cut in (0, 1):  # even height, then an odd height
        fr = frames[:, :frames.shape[1] - h_cut, :]
        n, h, w = fr.shape
        write_hex(work / 'reduce_in.hex', fr)
        code, log = iverilog(work, 'tb_vp_reduce', [PL / 'rtl' / 'vp_reduce.v', PL / 'sim' / 'tb_vp_reduce.v'],
                             {'W': w, 'H': h, 'N': n, 'MAXW': w})
        print(log.strip().splitlines()[-1] if log.strip() else '(no output)')
        got = read_hex(work / 'reduce_out.hex')
        want = np.concatenate([vm.pyr_reduce(f).reshape(-1) for f in fr]).astype(np.int64)
        if got.shape != want.shape:
            print('  %dx%d: %d outputs, expected %d' % (w, h, got.size, want.size))
            ok = False
            continue
        diff = int((got != want).sum())
        print('  %dx%d x%d frames: %s' % (w, h, n, 'identical' if diff == 0 else '%d pixels differ' % diff))
        ok = ok and diff == 0 and code == 0 and 'errors 0' in log
    return ok


def simulate(vis, frames, levels, coef_h, coef_l, gain, out_dir, keep=False):
    """vp_reduce chain + vp_core; returns [(magnified, amp)] per frame or None."""
    work = pathlib.Path(out_dir) / 'rtl'
    work.mkdir(parents=True, exist_ok=True)
    n, h, w = frames.shape
    lv = vm.working_levels(w, levels)
    write_hex(work / 'core_in.hex', frames)
    code, log = iverilog(work, 'tb_vp_core', [PL / 'rtl' / 'vp_reduce.v', PL / 'rtl' / 'vp_core.v',
                                              PL / 'sim' / 'tb_vp_core.v'],
                         {'W': w, 'H': h, 'N': n, 'LEVELS': lv, 'COEF_H': coef_h, 'COEF_L': coef_l, 'GAIN': gain})
    print(log.strip().splitlines()[-1] if log.strip() else '(no output)')
    if code != 0 or 'errors 0' not in log:
        print(log[-2000:])
        return None
    g0w, g0h = w, h
    for _ in range(lv):
        g0w, g0h = (g0w + 1) // 2, (g0h + 1) // 2
    w1, h1 = (g0w + 1) // 2, (g0h + 1) // 2
    mag = read_hex(work / 'core_mag.hex').reshape(n, g0h, g0w)
    amp = read_hex(work / 'core_amp.hex').reshape(n, h1, w1)
    if not keep:
        shutil.rmtree(work, ignore_errors=True)
    return [(mag[i], amp[i]) for i in range(n)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('what', choices=('reduce', 'core'))
    ap.add_argument('--synthetic', action='store_true')
    ap.add_argument('--frames', type=int, default=6)
    ap.add_argument('--keep', action='store_true')
    args = ap.parse_args()
    out = pathlib.Path(tempfile.gettempdir()) / 'vibmon_pcsim'
    work = out / 'rtl'
    work.mkdir(parents=True, exist_ok=True)
    if args.synthetic:
        frames = synthetic(args.frames)
        fps = 30.0
    else:
        import camera
        frames, times = camera.record(args.frames)
        fps = (len(times) - 1) / (times[-1] - times[0]) if len(times) > 1 else 30.0
    if args.what == 'reduce':
        ok = reduce_test(frames, work)
    else:
        ch, cl, gain = vm.lowpass_coef(6.0, fps), vm.lowpass_coef(1.0, fps), 20
        res = simulate(None, frames, 0, ch, cl, gain, out, keep=True)
        ok = res is not None
        if ok:
            st = None
            for i, f in enumerate(frames):
                o = vm.core(f, 0, ch, cl, gain, st)
                st = o.state
                dm = int((res[i][0] != o.mag).sum())
                da = int((res[i][1] != o.amp).sum())
                print('  frame %d: magnified %s, energy %s' % (i, 'identical' if dm == 0 else '%d differ' % dm,
                                                               'identical' if da == 0 else '%d differ' % da))
                ok = ok and dm == 0 and da == 0
    if ok and not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
