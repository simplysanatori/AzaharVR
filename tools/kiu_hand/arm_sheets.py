#!/usr/bin/env python3
# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Pictures of Pit's drawn arm, without the app (arm_render.py: the app's own shader on a
software rasteriser):

    python3 tools/kiu_hand/arm_sheets.py OUT_DIR [--clay] [--only shots,poses,first]

  shot_r1.png .. shot_r4.png   the four views the shape was judged in (an open left hand, a
                               left fist from the back, a right fist from in front and from
                               behind: the right arm is the body mirrored, without the plate)
  poses.png      a row a pose, three views each, as the app's inputs give them (top to bottom):
                 idle on the controller, trigger pulled, full fist, thumb out, pointing, the
                 palm open (no controller), the controller held with its handle drawn
  first.png      both arms as the player sees them, each in its own pose: left fist + right
                 pointing, left open + right fist, both on their handles

Reads no picture. The module's key poses are solved by poses.py (half a minute)."""
import argparse
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import arm_build as B     # noqa: E402
import arm_gear as G      # noqa: E402
import arm_pose as AP     # noqa: E402
import arm_render as RD   # noqa: E402
import make_arm           # noqa: E402
import poses as PZ        # noqa: E402


def linear(colour, surface):
    c = colour.astype(float) / 255
    c[..., :3] = RD.srgb_to_linear(c[..., :3])
    return c, surface.astype(float) / 255


def body_only(mesh):
    out = dict(mesh)
    out['tris'] = mesh['tris'][:len(mesh['tris']) - mesh['plate_tris']]
    return out


def mirrored(mesh):
    """The right arm: the body turned over (z), wound the other way, no plate."""
    out = body_only(mesh)
    flip = np.array([1.0, 1.0, -1.0])
    out['v'] = mesh['v'] * flip
    out['n'] = mesh['n'] * flip
    out['tan'] = np.concatenate([mesh['tan'][:, :3] * flip, -mesh['tan'][:, 3:]], 1)
    out['tris'] = out['tris'][:, ::-1]
    return out


def moved(mesh, offset):
    out = dict(mesh)
    out['v'] = mesh['v'] + np.asarray(offset, float)
    return out


def joined(a, b):
    out = {k: np.concatenate([a[k], b[k]]) for k in ('v', 'n', 'tan', 'uv')}
    out['tris'] = np.concatenate([a['tris'], b['tris'] + len(a['v'])])
    return out


def handle_mesh(handle, mesh):
    """The controller's handle as a dark rod (it takes the dark inside's texels)."""
    c, ax, rad = handle
    side = np.cross(ax, [0.0, 1.0, 0.0])
    side /= np.linalg.norm(side)
    up = np.cross(side, ax)
    n = 20
    ang = np.linspace(0, 2 * np.pi, n, endpoint=False)
    ring = np.cos(ang)[:, None] * side + np.sin(ang)[:, None] * up
    v = np.concatenate([c + ax * -0.075 + ring * rad, c + ax * 0.060 + ring * rad, [c + ax * -0.075], [c + ax * 0.060]])
    nn = np.concatenate([ring, ring, [-ax], [ax]])
    i = np.arange(n)
    j = (i + 1) % n
    tris = np.concatenate([np.stack([i, j, n + j], 1), np.stack([i, n + j, n + i], 1),
                           np.stack([j, i, np.full(n, 2 * n)], 1), np.stack([n + i, n + j, np.full(n, 2 * n + 1)], 1)])
    tris = G.orient(v, nn, tris)
    x, y, w, h = B.RECTS['floor_wrist']
    uv = np.tile([(x + w * 0.5 + 9) / B.TEX, (y + h * 0.5) / B.TEX], (len(v), 1))
    tan = np.tile([ax[0], ax[1], ax[2], 1.0], (len(v), 1))
    return dict(v=v, n=nn, tan=tan, uv=uv, tris=tris)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--clay', action='store_true')
    ap.add_argument('--only', default='shots,poses,first')
    ap.add_argument('--size', type=int, default=560)
    a = ap.parse_args()
    np.seterr(all='ignore')
    os.makedirs(a.out, exist_ok=True)
    quiet = lambda *x: None
    if a.clay:
        mesh, tex = B.assemble(log=quiet), None
        handle = AP.controller_fit()[0]
    else:
        mesh, colour, surface, handle, _, _, _ = make_arm.build(log=quiet)
        tex = linear(colour, surface)
    M = AP.Module(PZ.keys())

    def shot(m, eye, target, up, fov, size, bg=(1.0, 1.0, 1.0), key=None):
        if a.clay:
            return RD.clay(m, eye, target, up, fov, size, ss=2, bg=bg, key=key)
        return RD.to_srgb8(RD.painted(m, tex, eye, target, up, fov, size, ss=2, bg=bg, key=key), 2)

    def posed(local, held):
        return B.posed(mesh, *AP.on_arm(local, handle if held else None))

    inputs = dict(trigger=1, trigger_touch=True, grip=1, grip_touch=True)
    fist_free = M.from_inputs(thumb='rest', held=False, **inputs)
    open_free = M.from_curls(0.42, [0.42, 0.5, 0.56], 0.55, held=False)
    only = a.only.split(',')
    s = a.size
    if 'shots' in only:
        c = np.array
        views = [('r1', open_free, c([0.02, -0.015, 0.01]), c([0.378, 0.126, 0.315]), (0.45, 1, 0.0), False),
                 ('r2', fist_free, c([0.03, -0.005, 0.01]), c([0.40, 0.24, 0.02]), (-1, 0.3, -0.12), False),
                 ('r4', fist_free, c([-0.03, 0.0, 0.0]), c([0.20, 0.10, 0.46]), (-0.5, 1, 0), True),
                 ('r3', fist_free, c([-0.05, 0.0, 0.0]), c([-0.10, 0.30, 0.42]), (0.3, 1, 0), True)]
        for name, local, centre, off, up, right in views:
            m = posed(local, False)
            if right:
                m = mirrored(m)
                centre, off = centre * [1, 1, -1], off * [1, 1, -1]
            RD.write_png(os.path.join(a.out, f'shot_{name}.png'), shot(m, centre + off, centre, up, 24, (s, s)))
    if 'poses' in only:
        rows = [(M.from_inputs(), True, False), (M.from_inputs(trigger=1, trigger_touch=True, grip_touch=True, thumb='rest'), True, False),
                (M.from_inputs(thumb='rest', **inputs), True, False), (M.from_inputs(thumb=None, **inputs), True, False),
                (M.from_inputs(grip=1, grip_touch=True, thumb='rest'), True, False), (M.named('open'), False, False),
                (M.from_inputs(grip=0.6, grip_touch=True, trigger_touch=True, thumb='stick'), True, True)]
        c = np.array([0.035, -0.012, 0.0])
        cams = [((0.06, 0.50, 0.10), (1, 0, 0)), ((0.20, 0.10, 0.50), (0, 1, 0)), ((0.46, -0.14, 0.22), (0, 1, 0))]
        sheet = []
        for local, held, show in rows:
            m = posed(local, held)
            if show and not a.clay:
                m = joined(m, handle_mesh(handle, mesh))
            sheet.append(np.concatenate([shot(m, c + np.array(e), c, up, 27, (s * 3 // 4, s * 3 // 4)) for e, up in cams], 1))
        RD.write_png(os.path.join(a.out, 'poses.png'), np.concatenate(sheet, 0))
    if 'first' in only and not a.clay:
        pairs = [(M.from_inputs(thumb='rest', **inputs), True, M.from_inputs(grip=1, grip_touch=True, thumb='rest'), True),
                 (M.named('open'), False, fist_free, False),
                 (M.from_inputs(grip_touch=True, trigger_touch=True, thumb='stick'), True, M.from_inputs(thumb=None, **inputs), True)]
        ims = []
        for la, lh, ra, rh in pairs:
            both = joined(moved(posed(la, lh), (0, 0, -0.13)), moved(mirrored(posed(ra, rh)), (0, 0, 0.13)))
            ims.append(shot(both, (-0.62, 0.40, 0.0), (0.02, -0.02, 0.0), (0, 1, 0), 34, (s * 5 // 4, s), bg=(0.42, 0.36, 0.27),
                            key=(-0.3, 0.9, 0.35)))
        RD.write_png(os.path.join(a.out, 'first.png'), np.concatenate(ims, 0))


if __name__ == '__main__':
    main()
