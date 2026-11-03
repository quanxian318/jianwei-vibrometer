"""Stereo calibration of the AN5642 module for vibmon (PC side, OpenCV).

usage:
  python stereo_calibrate.py <pairs dir> <cols> <rows> <square_mm> <out.json> [--id NAME]
      <pairs dir>: left_*.png and right_*.png with the same suffix, full camera
      resolution, a printed checkerboard (cols x rows INNER corners) seen by
      both cameras; 15-30 pairs, board tilted and spread over the field.
  python stereo_calibrate.py --self-test <out dir>
      synthetic rig: recovers known intrinsics/baseline from projected corners,
      writes the JSON plus OpenCV rectification maps for cross-checking the
      board-side implementation (tests/test_stereo.cpp via stereo_eval --map).

Output JSON (read by StereoService / loadStereoCalibration):
  id, image_size [w, h], rms_px, baseline_mm,
  left/right: K (9), D (5: k1 k2 p1 p2 k3), R (9, rectifying rotation), P (12)
Fix focus, zoom and the mechanical mount before calibrating; any change
invalidates the file. Calibrate at the resolution the cameras run at.
"""
import glob
import json
import pathlib
import sys

import cv2
import numpy as np


def board_points(cols, rows, square):
    obj = np.zeros((rows * cols, 3), np.float32)
    obj[:, :2] = np.mgrid[0:cols, 0:rows].T.reshape(-1, 2) * square
    return obj


def calibrate(obj_pts, left_pts, right_pts, size):
    flags = cv2.CALIB_RATIONAL_MODEL * 0  # 5-parameter model, as the board-side remap
    _, k1, d1, _, _ = cv2.calibrateCamera(obj_pts, left_pts, size, None, None, flags=flags)
    _, k2, d2, _, _ = cv2.calibrateCamera(obj_pts, right_pts, size, None, None, flags=flags)
    rms, k1, d1, k2, d2, r, t, _, _ = cv2.stereoCalibrate(
        obj_pts, left_pts, right_pts, k1, d1, k2, d2, size, flags=cv2.CALIB_FIX_INTRINSIC,
        criteria=(cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 100, 1e-6))
    r1, r2, p1, p2, _, _, _ = cv2.stereoRectify(k1, d1, k2, d2, size, r, t, alpha=0,
                                                 flags=cv2.CALIB_ZERO_DISPARITY)
    return dict(rms=float(rms), K1=k1, D1=d1.ravel()[:5], K2=k2, D2=d2.ravel()[:5], R1=r1, R2=r2, P1=p1, P2=p2,
                T=t.ravel())


def to_json(c, size, name):
    def cam(k, d, r, p):
        return dict(K=[float(v) for v in k.ravel()], D=[float(v) for v in d], R=[float(v) for v in r.ravel()],
                    P=[float(v) for v in p.ravel()])
    return dict(id=name, image_size=[int(size[0]), int(size[1])], rms_px=c['rms'],
                baseline_mm=float(-c['P2'][0, 3] / c['P2'][0, 0]),
                left=cam(c['K1'], c['D1'], c['R1'], c['P1']), right=cam(c['K2'], c['D2'], c['R2'], c['P2']))


def from_images(folder, cols, rows, square):
    obj = board_points(cols, rows, square)
    obj_pts, lp, rp, size = [], [], [], None
    crit = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 50, 1e-3)
    for lf in sorted(glob.glob(str(pathlib.Path(folder) / 'left_*.png'))):
        rf = lf.replace('left_', 'right_')
        if not pathlib.Path(rf).exists():
            continue
        gl = cv2.imread(lf, cv2.IMREAD_GRAYSCALE)
        gr = cv2.imread(rf, cv2.IMREAD_GRAYSCALE)
        size = (gl.shape[1], gl.shape[0])
        ok1, c1 = cv2.findChessboardCorners(gl, (cols, rows))
        ok2, c2 = cv2.findChessboardCorners(gr, (cols, rows))
        if not (ok1 and ok2):
            print('skipped (board not found in both):', pathlib.Path(lf).name)
            continue
        obj_pts.append(obj)
        lp.append(cv2.cornerSubPix(gl, c1, (5, 5), (-1, -1), crit))
        rp.append(cv2.cornerSubPix(gr, c2, (5, 5), (-1, -1), crit))
    if len(obj_pts) < 8:
        raise SystemExit(f'only {len(obj_pts)} usable pairs; take at least 8 (15-30 recommended)')
    return obj_pts, lp, rp, size


def self_test(out_dir):
    out = pathlib.Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    size = (1280, 720)
    k = np.array([[1100.0, 0, 652], [0, 1098, 355], [0, 0, 1]])
    d1 = np.array([-0.12, 0.08, 0.0006, -0.0004, 0.0])
    d2 = np.array([-0.10, 0.06, -0.0005, 0.0003, 0.0])
    k2 = k.copy()
    k2[0, 2] += 9
    k2[1, 2] -= 6
    rvec = np.array([0.004, -0.012, 0.006])  # small mounting rotation of the right camera
    t = np.array([-60.0, 0.4, 0.8])           # 60 mm baseline
    obj = board_points(9, 6, 25.0)
    rng = np.random.default_rng(1)
    obj_pts, lp, rp = [], [], []
    for _ in range(25):
        rv = rng.uniform(-0.5, 0.5, 3)
        tv = np.array([rng.uniform(-150, 50), rng.uniform(-100, 0), rng.uniform(600, 1100)])
        pl, _ = cv2.projectPoints(obj, rv, tv, k, d1)
        r_board, _ = cv2.Rodrigues(rv)
        r_rel, _ = cv2.Rodrigues(rvec)
        rv2, _ = cv2.Rodrigues(r_rel @ r_board)
        tv2 = r_rel @ tv + t
        pr, _ = cv2.projectPoints(obj, rv2, tv2, k2, d2)
        pl = pl + rng.normal(0, 0.05, pl.shape)
        pr = pr + rng.normal(0, 0.05, pr.shape)
        inside = lambda p: np.all((p[..., 0] > 0) & (p[..., 0] < size[0]) & (p[..., 1] > 0) & (p[..., 1] < size[1]))
        if inside(pl) and inside(pr):
            obj_pts.append(obj)
            lp.append(pl.astype(np.float32))
            rp.append(pr.astype(np.float32))
    c = calibrate(obj_pts, lp, rp, size)
    j = to_json(c, size, 'synthetic-self-test')
    (out / 'stereo_calibration.json').write_text(json.dumps(j, indent=1))
    # OpenCV rectification maps at the working size 320x180 for the cross-check.
    for side, kk, dd, rr, pp in (('left', c['K1'], c['D1'], c['R1'], c['P1']), ('right', c['K2'], c['D2'], c['R2'], c['P2'])):
        mx, my = cv2.initUndistortRectifyMap(kk, dd, rr, pp, size, cv2.CV_32FC1)
        np.save(out / f'cv_map_{side}_x.npy', mx)
        np.save(out / f'cv_map_{side}_y.npy', my)
    err_f = abs(c['K1'][0, 0] - 1100) / 1100
    err_b = abs(j['baseline_mm'] - np.linalg.norm(t)) / np.linalg.norm(t)
    print(json.dumps(dict(pairs=len(obj_pts), rms_px=c['rms'], fx=c['K1'][0, 0], baseline_mm=j['baseline_mm'],
                          fx_rel_err=err_f, baseline_rel_err=err_b)))
    if err_f > 0.01 or err_b > 0.01 or c['rms'] > 0.2:
        raise SystemExit('self-test FAILED')
    print('self-test passed')


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == '--self-test':
        self_test(sys.argv[2])
        return
    if len(sys.argv) < 6:
        raise SystemExit(__doc__)
    folder, cols, rows, square, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4]), sys.argv[5]
    name = sys.argv[sys.argv.index('--id') + 1] if '--id' in sys.argv else pathlib.Path(out).stem
    obj_pts, lp, rp, size = from_images(folder, cols, rows, square)
    c = calibrate(obj_pts, lp, rp, size)
    j = to_json(c, size, name)
    pathlib.Path(out).write_text(json.dumps(j, indent=1))
    print(f"{len(obj_pts)} pairs, rms {c['rms']:.3f} px, baseline {j['baseline_mm']:.2f} mm, "
          f"rectified f {c['P1'][0, 0]:.1f} px -> {out}")
    if c['rms'] > 0.5:
        print('WARNING: reprojection error above 0.5 px; check board flatness, focus and corner detection')


if __name__ == '__main__':
    main()
