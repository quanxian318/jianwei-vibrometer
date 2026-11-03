"""Bit-exact comparison of the vision core: Python model vs C++ (vision_ref)
and, when available, the PL RTL simulation (Icarus Verilog in WSL).

    python check_bitexact.py                 # 40 webcam frames
    python check_bitexact.py --synthetic     # moving test pattern, no camera
    python check_bitexact.py --vis clip.vis  # a recorded clip
    python check_bitexact.py --rtl           # also simulate the PL core

Writes compare_<name>.png (input | G0 | magnified | energy) into --out
(default: %TEMP%ibmon_pcsim).
"""
import argparse
import pathlib
import subprocess
import sys
import tempfile

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import vision_model as vm  # noqa: E402

ROOT = HERE.parent
VIBMON = ROOT / 'ax7010' / 'linux' / 'vibmon'
WSL_BUILD = '/tmp/vibmon_pcsim'


def wsl_path(p):
    p = pathlib.Path(p).resolve()
    drive = p.drive.rstrip(':').lower()
    return '/mnt/%s/%s' % (drive, p.as_posix().split(':/', 1)[1])


def wsl(cmd, timeout=900):
    r = subprocess.run(['wsl.exe', '-e', 'bash', '-lc', cmd], capture_output=True, timeout=timeout)
    out = r.stdout.decode('utf-8', 'replace') + r.stderr.decode('utf-8', 'replace')
    return r.returncode, out


def synthetic(n=40, w=640, h=480, fps=30.0, f_hz=4.0, amp_px=1.5):
    yy, xx = np.mgrid[0:h, 0:w]
    rng = np.random.default_rng(1)
    noise = rng.normal(0, 2, (n, h, w))
    frames = []
    for i in range(n):
        dy = amp_px * np.sin(2 * np.pi * f_hz * i / fps)
        v = 90 + 40 * np.sin(xx * 0.09) * np.cos(yy * 0.06)
        beam = np.abs(yy - (h * 0.45 + dy)) < h * 0.05
        v = np.where(beam & (xx > w * 0.25) & (xx < w * 0.8), 210, v)
        frames.append(np.clip(v + noise[i], 0, 255).astype(np.uint8))
    return np.stack(frames)


def compare(name, a, b):
    a = np.asarray(a).astype(np.int64)
    b = np.asarray(b).astype(np.int64)
    if a.shape != b.shape:
        return '%s: shape %s vs %s' % (name, a.shape, b.shape)
    d = np.abs(a - b)
    return None if d.max() == 0 else '%s: %d values differ, max |diff| %d' % (name, int((d > 0).sum()), int(d.max()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--vis', help='recorded clip (VIS1)')
    ap.add_argument('--synthetic', action='store_true')
    ap.add_argument('--frames', type=int, default=40)
    ap.add_argument('--fps', type=float, default=30.0)
    ap.add_argument('--low', type=float, default=1.0)
    ap.add_argument('--high', type=float, default=6.0)
    ap.add_argument('--gain', type=int, default=20)
    ap.add_argument('--levels', type=int, default=0)
    ap.add_argument('--rtl', action='store_true', help='also run the PL testbench')
    ap.add_argument('--out', default=str(pathlib.Path(tempfile.gettempdir()) / 'vibmon_pcsim'))
    args = ap.parse_args()
    out_dir = pathlib.Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.vis:
        frames, levels, coef_h, coef_l, gain = vm.read_vis(args.vis)
        vis = pathlib.Path(args.vis)
    else:
        if args.synthetic:
            frames = synthetic(args.frames, fps=args.fps)
        else:
            import camera
            frames, times = camera.record(args.frames)
            if len(times) > 1:
                args.fps = (len(times) - 1) / (times[-1] - times[0])
            print('webcam: %d frames %dx%d, %.1f fps' % (len(frames), frames.shape[2], frames.shape[1], args.fps))
        levels = args.levels
        coef_h = vm.lowpass_coef(args.high, args.fps)
        coef_l = vm.lowpass_coef(args.low, args.fps)
        gain = args.gain
        vis = out_dir / ('synthetic.vis' if args.synthetic else 'webcam.vis')
        vm.write_vis(vis, frames, levels, coef_h, coef_l, gain)
    print('clip %s: %d frames %dx%d, levels %d, coef H %d L %d, gain %d' % (
        vis.name, len(frames), frames.shape[2], frames.shape[1], levels, coef_h, coef_l, gain))

    # Python model.
    st, model = None, []
    for f in frames:
        o = vm.core(f, levels, coef_h, coef_l, gain, st)
        st = o.state
        model.append((o.g0, o.mag, o.band, o.amp))

    # C++ reference.
    vout = vis.with_suffix('.vout')
    code, log = wsl('make -s -C "%s" BUILD=%s %s/vision_ref && %s/vision_ref "%s" "%s"' % (
        wsl_path(VIBMON), WSL_BUILD, WSL_BUILD, WSL_BUILD, wsl_path(vis), wsl_path(vout)))
    if code != 0:
        print('C++ reference failed:\n' + log)
        return 1
    cpp = vm.read_vout(vout)
    problems = []
    for i, (m, c) in enumerate(zip(model, cpp)):
        for name, a, b in zip(('g0', 'magnified', 'band', 'amp'), m, c):
            p = compare('frame %d %s' % (i, name), a, b)
            if p:
                problems.append(p)
    print('Python vs C++: %s' % ('identical (%d frames, 4 outputs each)' % len(model) if not problems else
                                 '%d differences, first: %s' % (len(problems), problems[0])))

    if args.rtl:
        import run_rtl
        rtl = run_rtl.simulate(vis, frames, levels, coef_h, coef_l, gain, out_dir)
        if rtl is None:
            problems.append('RTL simulation failed')
        else:
            rp = []
            for i, (m, r) in enumerate(zip(model, rtl)):
                for name, a, b in zip(('magnified', 'amp'), (m[1], m[3]), r):
                    p = compare('frame %d %s' % (i, name), a, b)
                    if p:
                        rp.append(p)
            print('Python vs RTL: %s' % ('identical (%d frames)' % len(rtl) if not rp else
                                         '%d differences, first: %s' % (len(rp), rp[0])))
            problems += rp

    # Picture of the last frame.
    try:
        from PIL import Image
        g0, mag, band, amp = model[-1]
        e = vm.energy_display(amp)
        e_up = np.kron(e, np.ones((2, 2), np.uint8))[:g0.shape[0], :g0.shape[1]]
        small = np.asarray(Image.fromarray(frames[-1]).resize((g0.shape[1], g0.shape[0]), Image.BILINEAR))
        strip = np.concatenate([small, g0, mag, e_up], axis=1)
        img = Image.fromarray(strip).resize((strip.shape[1] * 3, strip.shape[0] * 3), Image.NEAREST)
        png = out_dir / ('compare_%s.png' % vis.stem)
        img.save(png)
        print('wrote', png)
    except Exception as ex:
        print('picture skipped:', ex)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
