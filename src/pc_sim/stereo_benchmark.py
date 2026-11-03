"""Stereo matcher benchmark against Middlebury ground truth.

usage: python stereo_benchmark.py <stereo_eval binary> <dataset dir> <out.json> [--save-maps dir]

<dataset dir> holds <scene>_im2.png, <scene>_im6.png, <scene>_disp2.png
(opencv_extra testdata/cv/stereomatching/datasets layout; GT scale per scene
below, 0 = unknown) and optionally motorcycle_*.npy from scikit-image.

Conditions:
  clean      original pair
  exposure   right image: gain 0.8, offset +12, gamma 1.15, noise sigma 2
             (two free-running OV5640 with independent auto exposure)
  misalign   exposure + right image shifted 0.75 px vertically
             (residual rectification error)
Metrics on pixels with known GT (8 px border excluded):
  density    measured output / known (hole-filled pixels excluded)
  bad2       |d - gt| > 2 px among valid output (wrong but reported)
  bad2_all   (invalid or bad) / known
  rmse       among valid output, px
  shown_density, shown_bad2  the same for the displayed map (measured + filled)
"""
import json
import pathlib
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

SCALE = {'cones': 4, 'teddy': 4, 'tsukuba': 16, 'venus': 8, 'barn2': 8, 'bull': 8, 'poster': 8, 'sawtooth': 8}
METHODS = {
    'bm_team': ['bm'],                                           # team PL BM + V1.2 spatial support
    'pl_core': ['sgm', 'census=9', 'paths=2', 'fill=0', 'refine=0', 'median=0', 'speckle=0', 'contrast=0'],
                                                                 # PL stereo_core output alone (bit-exact model)
    'pl_core_ps_post': ['sgm', 'census=9', 'paths=2'],           # vibmon default: PL core + PS post (texture gate, LK refine, median, speckle, hole fill)
    'pl_core_ps_post_nofill': ['sgm', 'census=9', 'paths=2', 'fill=0'],
    'ps_sgm_8path': ['sgm', 'census=7', 'paths=8'],              # PS-only reference
}


def gray(rgb):
    rgb = np.asarray(rgb, dtype=np.float64)
    return rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114 if rgb.ndim == 3 else rgb


def perturb(right, condition, rng):
    r = right.astype(np.float64)
    if condition in ('exposure', 'misalign'):
        r = 255.0 * (np.clip(r * 0.8 + 12, 0, 255) / 255.0) ** 1.15 + rng.normal(0, 2, r.shape)
    if condition == 'misalign':
        a = 0.75
        r = (1 - a) * r + a * np.vstack([r[1:], r[-1:]])
    return np.clip(np.rint(r), 0, 255).astype(np.uint8)


def write_pgm(path, img):
    h, w = img.shape
    with open(path, 'wb') as f:
        f.write(b'P5 %d %d 255\n' % (w, h))
        f.write(np.ascontiguousarray(img, dtype=np.uint8).tobytes())


def scenes(root):
    root = pathlib.Path(root)
    for name, s in SCALE.items():
        if (root / f'{name}_im2.png').exists():
            l = gray(Image.open(root / f'{name}_im2.png').convert('RGB'))
            r = gray(Image.open(root / f'{name}_im6.png').convert('RGB'))
            gt = np.asarray(Image.open(root / f'{name}_disp2.png').convert('L'), dtype=np.float64) / s
            yield name, np.rint(l).astype(np.uint8), np.rint(r).astype(np.uint8), gt
    if (root / 'motorcycle_left.npy').exists():
        l = gray(np.load(root / 'motorcycle_left.npy'))
        r = gray(np.load(root / 'motorcycle_right.npy'))
        gt = np.load(root / 'motorcycle_disp.npy').astype(np.float64)
        gt[~np.isfinite(gt)] = 0
        yield 'motorcycle', np.rint(l).astype(np.uint8), np.rint(r).astype(np.uint8), gt


def main():
    tool, data, out = sys.argv[1], sys.argv[2], sys.argv[3]
    save = pathlib.Path(sys.argv[sys.argv.index('--save-maps') + 1]) if '--save-maps' in sys.argv else None
    rng = np.random.default_rng(20261021)
    results = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        for name, left, right, gt in scenes(data):
            known = gt > 0
            known[:8], known[-8:], known[:, :8], known[:, -8:] = False, False, False, False
            for cond in ('clean', 'exposure', 'misalign'):
                write_pgm(tmp / 'l.pgm', left)
                write_pgm(tmp / 'r.pgm', perturb(right, cond, rng))
                for method, args in METHODS.items():
                    info = json.loads(subprocess.check_output(
                        [tool, str(tmp / 'l.pgm'), str(tmp / 'r.pgm'), str(tmp / 'd.raw'), *args, 'disp=64']))
                    d = np.fromfile(tmp / 'd.raw', dtype='<i2').reshape(left.shape)
                    mask = tmp / 'd.raw.filled'
                    filled = np.fromfile(mask, dtype=np.uint8).reshape(left.shape) > 0 if mask.exists() else np.zeros_like(known)
                    shown = (d >= 0) & known          # measured + filled (display)
                    valid = shown & ~filled            # measured only
                    err = np.abs(d / 16.0 - gt)
                    nk, nv = int(known.sum()), int(valid.sum())
                    bad = int((err[valid] > 2).sum())
                    ns, bad_s = int(shown.sum()), int((err[shown] > 2).sum())
                    results.append(dict(scene=name, condition=cond, method=method, known=nk, valid=nv,
                                        density=nv / nk, bad2=bad / max(nv, 1), bad2_all=(nk - nv + bad) / nk,
                                        shown_density=ns / nk, shown_bad2=bad_s / max(ns, 1),
                                        rmse=float(np.sqrt(np.mean(err[valid] ** 2))) if nv else None, ms=info['ms']))
                    if save is not None and cond in ('clean', 'exposure'):
                        save.mkdir(parents=True, exist_ok=True)
                        np.save(save / f'{name}_{cond}_{method}.npy', d)
                    r = results[-1]
                    print(f"{name:10s} {cond:9s} {method:18s} density {r['density']:.3f} bad2 {r['bad2']:.3f} "
                          f"bad2_all {r['bad2_all']:.3f} rmse {r['rmse'] or 0:.2f} {r['ms']:.0f} ms", flush=True)
    pathlib.Path(out).write_text(json.dumps(results, indent=1))


if __name__ == '__main__':
    main()
