#!/usr/bin/env python3
# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Preview sheets of Pit's hand from what the app's own code makes (dump.cpp's files):

    python3 tools/kiu_hand/sheets.py --dump DUMP_DIR --out OUT_DIR
        [--old-arm pit-arm.model --fits armfits.txt] [--controller frame_controller_right.model]

Needs numpy and Pillow. The optional files are a player's own (the game's arm as the app
converted it on this machine, the controller's model made at install time): they are only
drawn beside ours for comparison, in images that stay out of the repository."""
import argparse
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render as R  # noqa: E402

UNIT = 0.075
HEAD = np.array([0.0, 0.42, 0.38])            # the eyes, over two controllers held level
CTRL_L = np.array([-0.18, 0.0, 0.0])


def read_mesh(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'KHM1', path
    vc, ic = struct.unpack_from('<II', d, 4)
    v = np.frombuffer(d, '<f4', vc * 9, 12).reshape(vc, 9).astype(float)
    i = np.frombuffer(d, '<u4', ic, 12 + vc * 36).reshape(-1, 3).astype(int)
    return v[:, :3], v[:, 3:6], v[:, 6:9], i


def read_model(path):
    """An MXFM model's triangles (position, normal, uv, indices, components, texture)."""
    d = open(path, 'rb').read()
    magic, ver, vc, ic, cc, tw, th, flags = struct.unpack_from('<4s7I', d, 0)
    assert magic == b'MXFM'
    o = 32 + cc * 208
    v = np.frombuffer(d, '<f4', vc * 12, o).reshape(vc, 12).astype(float)
    o += vc * 48
    i = np.frombuffer(d, '<u4', ic, o).reshape(-1, 3).astype(int)
    o += ic * 4
    tex = np.frombuffer(d, 'u1', tw * th * 4, o).reshape(th, tw, 4)
    return v, i, tex


def affine(m16, p, n=None):
    a = np.array(m16, float).reshape(4, 4).T
    q = p @ a[:3, :3].T + a[:3, 3]
    if n is None:
        return q
    lin = np.linalg.inv(a[:3, :3]).T
    nn = n @ lin.T
    return q, nn / np.linalg.norm(nn, axis=1, keepdims=True)


def info(path):
    out = {}
    for line in open(path):
        f = line.split()
        if f[0] == 'controller_fit':
            out['fit_' + f[1]] = [float(x) for x in f[2:18]]
        elif f[0] == 'seat':
            out['seat_' + f[1]] = [float(x) for x in f[2:5]]
        else:
            out[f[0]] = f[1:]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dump', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--old-arm')
    ap.add_argument('--fits')
    ap.add_argument('--controller')
    ap.add_argument('--only')
    ap.add_argument('--size', type=int, default=340)
    a = ap.parse_args()
    np.seterr(all='ignore')
    os.makedirs(a.out, exist_ok=True)
    inf = info(os.path.join(a.dump, 'info.txt'))
    fit_l = inf['fit_left']
    S = a.size

    def hand(name, fit=fit_l, at=CTRL_L, flip=False):
        """A dumped mesh placed on the left controller (aim space level at `at`)."""
        v, n, c, i = read_mesh(os.path.join(a.dump, name + '.mesh'))
        v, n = affine(fit, v, n)
        if flip:
            i = i[:, ::-1]
        return (v + at, n, c, i)

    def arm_space(name):
        v, n, c, i = read_mesh(os.path.join(a.dump, name + '.mesh'))
        return (v * UNIT, n, c, i)

    controller = None
    if a.controller:
        v, i, _ = read_model(a.controller)
        p, n = v[:, :3].copy(), v[:, 3:6].copy()
        p[:, 0] *= -1   # the left controller: the right one's mirror
        n[:, 0] *= -1
        controller = (p + CTRL_L, n, np.tile([0.30, 0.31, 0.35], (len(p), 1)), i[:, ::-1])

    grip_at = CTRL_L + np.array([-0.013, -0.0444, 0.0871])   # the handle's middle (weapon_fit.h, mirrored)
    views = {
        'eye view': (HEAD, grip_at + np.array([0, 0.02, -0.02]), (0, 1, 0), 26),
        'front': (grip_at + np.array([0.0, 0.10, -0.55]), grip_at, (0, 1, 0), 26),
        'back of the hand': (grip_at + np.array([-0.55, 0.08, 0.02]), grip_at, (0, 1, 0), 26),
        'palm side': (grip_at + np.array([0.55, 0.08, 0.02]), grip_at, (0, 1, 0), 26),
        'above': (grip_at + np.array([0.0, 0.6, 0.03]), grip_at + np.array([0, 0, 0.03]), (0, 0, -1), 26),
    }

    def shots(meshes, title, which, size=S):
        out = []
        for name in which:
            eye, at, up, fov = views[name]
            out.append(R.label(R.draw(meshes, R.look(eye, at, up), size=size, fov_deg=fov), f'{title}: {name}'))
        return out

    def want(name):
        return not a.only or name in a.only.split(',')

    free = ['open', 'relaxed', 'fist', 'point', 'thumb_up']
    held = ['grip', 'trigger', 'grip_thumb_up', 'grip_stick', 'grip_buttons', 'grip_open']
    order = ['eye view', 'front', 'back of the hand', 'palm side', 'above']
    if want('poses-free'):
        ims = []
        for p in free:
            ims += shots([hand(p)], p, order)
        R.grid(ims, len(order)).save(os.path.join(a.out, 'poses-free.png'))
    if want('poses-held'):
        ims = []
        for p in held:
            ims += shots([hand(p + '_elbow')] + ([controller] if controller else []), p, order)
        R.grid(ims, len(order)).save(os.path.join(a.out, 'poses-held.png'))
    if want('poses-held-bare'):
        ims = []
        for p in held:
            ims += shots([hand(p + '_elbow')], p + ' (no controller)', order)
        R.grid(ims, len(order)).save(os.path.join(a.out, 'poses-held-bare.png'))

    # Close: the hand alone in its own space, big.
    def close(names, file, cols):
        ims = []
        c = np.array([0.055, -0.005, 0.005])
        cams = [('back', c + [0, 0.42, 0], (1, 0, 0)), ('palm', c + [0, -0.42, 0], (1, 0, 0)),
                ('thumb side', c + [0.02, 0.06, 0.42], (0, 1, 0)), ('three quarter', c + [0.26, 0.24, 0.22], (0, 1, 0)),
                ('finger tips', c + [0.40, 0.08, 0.10], (0, 1, 0)), ('from the elbow', c + [-0.40, 0.20, 0.14], (0, 0, 1))]
        for nm in names:
            m = arm_space(nm)
            for label, eye, up in cams[:cols]:
                ims.append(R.label(R.draw([m], R.look(eye, c, up), size=420, fov_deg=26), f'{nm}: {label}'))
        R.grid(ims, cols).save(os.path.join(a.out, file))

    if want('close'):
        close(['relaxed', 'fist', 'grip', 'point'], 'close.png', 6)
    if want('bind'):
        close(['bind', 'open'], 'bind.png', 6)

    if want('compare') and a.old_arm and a.fits:
        # The game's arm as the app draws it today (the off hand's fit) beside ours.
        fits = {}
        for line in open(a.fits):
            f = line.split()
            fits[(int(f[0]), f[1], f[2])] = [float(x) for x in f[4:20]]
        off = fits[(0, 'L', 'O')]
        v, i, tex = read_model(a.old_arm)
        p, n = affine(off, v[:, :3], v[:, 3:6])
        uv = v[:, 10:12]
        th, tw = tex.shape[:2]
        tx = np.clip((uv[:, 0] % 1.0) * tw, 0, tw - 1).astype(int)
        ty = np.clip((uv[:, 1] % 1.0) * th, 0, th - 1).astype(int)
        col = tex[ty, tx, :3] / 255.0
        old = (p + CTRL_L, n, col, i)
        rows = [('the arm today (the game\'s fist)', [old], True),
                ('ours, fist, the same fit', [hand('fist', off)], False),
                ('ours, relaxed, the same fit', [hand('relaxed', off)], False),
                ('ours, grip, on the controller, forearm to the elbow', [hand('grip_elbow')], False)]
        ims = []
        for title, meshes, flat in rows:
            for name in order:
                eye, at, up, fov = views[name]
                ims.append(R.label(R.draw(meshes, R.look(eye, at, up), size=S, fov_deg=fov, flat=flat),
                                   f'{title}: {name}'))
        R.grid(ims, len(order)).save(os.path.join(a.out, 'compare.png'))

    if want('wrist'):
        # The wrist screen with its laurel frame on the vambrace: a dark screen stands in.
        ims = []
        for side in ('back', 'inside'):
            lv, ln, lc, li = arm_space('worn_' + side)
            seat = np.array(inf['seat_' + side]) * UNIT
            sgn = 1.0 if side == 'back' else -1.0
            w = 0.10
            zc = seat[1] + sgn * 0.040 * w
            quad = np.array([[seat[0] - w / 2, zc, sgn * 0.0375], [seat[0] + w / 2, zc, sgn * 0.0375],
                             [seat[0] + w / 2, zc, -sgn * 0.0375], [seat[0] - w / 2, zc, -sgn * 0.0375]])
            qn = np.tile([0, sgn, 0.0], (4, 1))
            qc = np.tile([0.10, 0.30, 0.38], (4, 1))
            screen = (quad, qn, qc, np.array([[0, 2, 1], [0, 3, 2], [0, 1, 2], [0, 2, 3]]))
            scene = [arm_space('relaxed'), (lv, ln, lc, li), screen]
            c = np.array([-0.05, 0.0, 0.0])
            cams = [('worn on the ' + side + ' of the vambrace', c + [0.10, sgn * 0.42, 0.12], (1, 0, 0), 30),
                    ('from the wearer\'s eye', c + [-0.30, sgn * 0.26, 0.20], (0, 0, 1) if side == 'back' else (0, 0, 1), 30),
                    ('close', np.array([-0.08, sgn * 0.26, 0.05]), (1, 0, 0), 28),
                    ('edge on', c + [-0.03, sgn * 0.05, 0.40], (0, 1, 0), 30)]
            for label, eye, up, fov in cams:
                at = c if label != 'close' else np.array([-0.08, 0, 0])
                ims.append(R.label(R.draw(scene, R.look(eye, at, up), size=460, fov_deg=fov), label))
        R.grid(ims, 4).save(os.path.join(a.out, 'wrist-panel.png'))
    if want('laurel'):
        lv, ln, lc, li = read_mesh(os.path.join(a.dump, 'laurel.mesh'))
        pv = np.array([[-.5, -.375, 0], [.5, -.375, 0], [.5, .375, 0], [-.5, .375, 0]], float)
        panel = (pv, np.tile([0, 0, 1.0], (4, 1)), np.tile([0.10, 0.30, 0.38], (4, 1)), np.array([[0, 1, 2], [0, 2, 3]]))
        ms = [(lv, ln, lc, li), panel]
        ims = [R.label(R.draw(ms, R.look((0, 0, 3.3), (0, 0, 0)), size=640, fov_deg=30), 'laurel frame: front'),
               R.label(R.draw(ms, R.look((1.5, -1.5, 2.0), (0, 0, 0), (0, 0, 1)), size=640, fov_deg=30), 'laurel frame: raking')]
        R.grid(ims, 2).save(os.path.join(a.out, 'laurel.png'))
    if want('extras'):
        fit_r = inf['fit_right']
        rows = [('right hand (the left mesh, the mirroring fit)', [hand('grip_elbow', fit_r, -CTRL_L, flip=True)]),
                ('forearm turned to the elbow', [hand('grip_elbow')]),
                ('hand tracking: relaxed fed back', [hand('tracked_relaxed')]),
                ('free, inputs half', [hand('free_half')]),
                ('held, inputs half', [hand('held_half')]),
                ('other colours (as from a game texture)', [hand('relaxed_other_colours')])]
        ims = []
        for title, meshes in rows:
            if 'right' in title:
                g = -CTRL_L + np.array([0.013, -0.0444, 0.0871])
                for name in ('eye view', 'front', 'above'):
                    eye, at, up, fov = views[name]
                    eye = eye + (g - grip_at) if name != 'eye view' else eye
                    ims.append(R.label(R.draw(meshes, R.look(eye, g + (at - grip_at), up), size=S, fov_deg=fov), f'{title}: {name}'))
            else:
                ims += shots(meshes, title, ('eye view', 'front', 'above'))
        R.grid(ims, 3).save(os.path.join(a.out, 'extras.png'))


if __name__ == '__main__':
    main()
