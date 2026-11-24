# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Pit's arm, drawn: what the forearm wears, built straight from numbers: two thick flat-sided
gold bands standing off the arm, the barrel of leather between them, the dark inside seen
through each band, the bare forearm fading out past the elbow, and (the left arm's alone) the
pointed red plate with its bevelled gold border over the back of the wrist.

Each part: v, n (hard edges already split), tris, uv (its own chart's units), tri material."""
import numpy as np

import arm_skel as S
from arm_skel import norm

GOLD, ENAMEL, LEATHER, SKIN, CLOTH, DARK = 0, 1, 2, 3, 4, 5   # DARK: cloth, nearly black

SIDES = 40
BAND_RI, BAND_RO, BAND_HALF, BAND_BEVEL = 0.0450, 0.0610, 0.0130, 0.0032
BAND_WRIST_X, BAND_ELBOW_X = -0.0300, -0.1380
LEATHER_X = (BAND_ELBOW_X - 0.008, BAND_WRIST_X + 0.008)
LEATHER_R = (0.0436, 0.0492)     # at the bands, in the middle: a barrel
FLOOR_IN = 0.0070                # how far inside a band its dark floor is
STUB_X = (-0.1480, -0.2000)      # the bare forearm past the elbow band, and where it ends
STUB_FADE = (-0.1560, -0.1960)   # solid before, gone after
ARM_HALF = (0.0235, 0.0288)      # the forearm's half thickness and half width at the wrist
ELBOW_HALF = (0.0290, 0.0335)    # and past the elbow band


def leather_radius(x):
    t = (x - LEATHER_X[0]) / (LEATHER_X[1] - LEATHER_X[0])
    return LEATHER_R[0] + (LEATHER_R[1] - LEATHER_R[0]) * np.sin(np.pi * np.clip(t, 0, 1)) ** 0.8


def orient(v, n, tris):
    """Turns each triangle to face the way its vertices' normals do."""
    fn = np.cross(v[tris[:, 1]] - v[tris[:, 0]], v[tris[:, 2]] - v[tris[:, 0]])
    flip = (fn * n[tris].sum(1)).sum(1) < 0
    tris = tris.copy()
    tris[flip] = tris[flip][:, ::-1]
    return tris


def lathe(profile, smooth=False, sides=SIDES, squash=(1.0, 1.0)):
    """Turns a profile [(x, r)] round the arm's line. Hard edges between its segments unless
    `smooth`. uv: (turn 0..1, length along the profile in metres)."""
    pr = np.array(profile, float)
    seg = pr[1:] - pr[:-1]
    n2 = np.stack([-seg[:, 1], seg[:, 0]], 1)
    n2 /= np.maximum(np.linalg.norm(n2, axis=1, keepdims=True), 1e-12)
    length = np.concatenate([[0], np.cumsum(np.linalg.norm(seg, axis=1))])
    ang = np.linspace(0, 2 * np.pi, sides + 1)
    cy, sz = np.cos(ang), np.sin(ang)
    vs, ns, uvs, ts = [], [], [], []

    def row(pt, nr, ln):
        v = np.stack([np.full(sides + 1, pt[0]), pt[1] * cy * squash[0], pt[1] * sz * squash[1]], 1)
        n = norm(np.stack([np.full(sides + 1, nr[0]), nr[1] * cy / squash[0], nr[1] * sz / squash[1]], 1))
        return v, n, np.stack([ang / (2 * np.pi), np.full(sides + 1, ln)], 1)

    def strip(a, b):
        i = np.arange(sides)
        return np.concatenate([np.stack([a + i, a + i + 1, b + i + 1], 1), np.stack([a + i, b + i + 1, b + i], 1)])

    at = 0
    if smooth:
        pn = np.zeros_like(pr)
        pn[:-1] += n2
        pn[1:] += n2
        pn /= np.linalg.norm(pn, axis=1, keepdims=True)
        for k in range(len(pr)):
            v, n, uv = row(pr[k], pn[k], length[k])
            vs.append(v), ns.append(n), uvs.append(uv)
        for k in range(len(pr) - 1):
            ts.append(strip(k * (sides + 1), (k + 1) * (sides + 1)))
    else:
        for k in range(len(seg)):
            for e in (0, 1):
                v, n, uv = row(pr[k + e], n2[k], length[k + e])
                vs.append(v), ns.append(n), uvs.append(uv)
            ts.append(strip(at, at + sides + 1))
            at += 2 * (sides + 1)
    v, n, uv, t = np.concatenate(vs), np.concatenate(ns), np.concatenate(uvs), np.concatenate(ts)
    return v, n, orient(v, n, t), uv


def band(xc):
    h, b = BAND_HALF, BAND_BEVEL
    prof = [(xc - h, BAND_RI), (xc - h, BAND_RO - b), (xc - h + b, BAND_RO), (xc + h - b, BAND_RO),
            (xc + h, BAND_RO - b), (xc + h, BAND_RI), (xc - h, BAND_RI)]
    return lathe(prof)


def leather():
    xs = np.linspace(LEATHER_X[0], LEATHER_X[1], 7)
    return lathe([(x, leather_radius(x)) for x in xs], smooth=True)


def floor(x, facing):
    """The dark inside of a band: a ring across it, facing out of it (`facing` +1: toward the
    hand)."""
    prof = [(x, 0.0040), (x - facing * 0.004, BAND_RI + 0.0004)]
    if facing > 0:
        prof = prof[::-1]
    v, n, t, uv = lathe(prof, sides=SIDES // 2)
    uv = np.stack([v[:, 1], v[:, 2]], 1)
    return v, n, t, uv


def stub():
    """The bare forearm past the elbow band: closed and rounded at its far end."""
    x0, x1 = STUB_X
    prof = []
    for t in np.linspace(0, 1, 4):
        prof.append((x0 + (x1 + 0.016 - x0) * t, 1.0))
    for a in np.radians([30, 60, 85]):
        prof.append((x1 + 0.016 - 0.016 * np.sin(a), np.cos(a)))
    prof = [(x, r * 0.0320) for x, r in prof][::-1]
    v, n, t, uv = lathe(prof, smooth=True, sides=16, squash=(ELBOW_HALF[0] / 0.0320, ELBOW_HALF[1] / 0.0320))
    # close the tip
    tip = np.array([[x1 - 0.0002, 0, 0]])
    first = np.arange(16)
    v = np.concatenate([v, tip])
    n = np.concatenate([n, [[-1.0, 0, 0]]])
    uv = np.concatenate([uv, [[0.5, uv[:, 1].min() - 0.002]]])
    cap = np.stack([first, first + 1, np.full(16, len(v) - 1)], 1)
    t = np.concatenate([t, cap])
    return v, n, orient(v, n, t), uv


# ---- the plate ----
PLATE_X = (-0.0360, 0.0120, 0.0380)   # its root under the wrist band, its shoulders, its point
PLATE_W = (0.0350, 0.0360)            # half width at the root, at the shoulders
PLATE_Y = (0.0285, 0.0305)            # its underside's height at the root, at the point
PLATE_ARCH = 0.0125                   # how far its sides fall
PLATE_T = 0.0036                      # thickness
PLATE_BORDER = 0.0078                 # the gold border's width
PLATE_INSET = 0.0017                  # how far the red is set in
PLATE_Z = 0.0010


def plate_outline():
    x0, x1, x2 = PLATE_X
    w0, w1 = PLATE_W
    pts = []

    def run(a, b, n):
        for i in range(n):
            pts.append(np.array(a) + (np.array(b) - np.array(a)) * i / n)

    run((x0, -w0), (x1, -w1), 3)
    run((x1, -w1), (x2, 0.0), 3)
    run((x2, 0.0), (x1, w1), 3)
    run((x1, w1), (x0, w0), 3)
    run((x0, w0), (x0, -w0), 6)
    return np.array(pts)


def inset_ring(o, d):
    """The outline moved in by d (2D, along each corner's inward normal)."""
    prev, nxt = np.roll(o, 1, 0), np.roll(o, -1, 0)

    def inward(a, b):
        e = b - a
        n = np.stack([-e[:, 1], e[:, 0]], 1)
        return n / np.linalg.norm(n, axis=1, keepdims=True)

    n1, n2 = inward(prev, o), inward(o, nxt)
    c = o.mean(0)
    s = np.sign(((c - o) * n1).sum(1))[:, None]
    n1, n2 = n1 * s, n2 * np.sign(((c - o) * n2).sum(1))[:, None]
    m = n1 + n2
    m /= np.linalg.norm(m, axis=1, keepdims=True)
    return o + m * (d / np.maximum((m * n1).sum(1), 0.5))[:, None]


def plate_height(q):
    """The plate's underside over a point (x, z): arched across, a soft ridge down its middle,
    lifting off the hand toward its point."""
    x0, _, x2 = PLATE_X
    t = np.clip((q[:, 0] - x0) / (x2 - x0), 0, 1)
    z = np.abs(q[:, 1]) / PLATE_W[0]
    return PLATE_Y[0] + (PLATE_Y[1] - PLATE_Y[0]) * t - PLATE_ARCH * (0.55 * z + 0.45 * z * z)


def plate():
    """Returns top (the face: border and inset) and under (the rim wall and the underside), each
    v, n, tris, uv, and the top's triangle materials."""
    o = plate_outline()
    n = len(o)
    i1 = inset_ring(o, PLATE_BORDER * 0.22)
    i2 = inset_ring(o, PLATE_BORDER * 0.80)
    i3 = inset_ring(o, PLATE_BORDER)

    def ring(q, lift):
        return np.stack([q[:, 0], plate_height(q) + lift, q[:, 1] + PLATE_Z], 1)

    rings = [ring(o, PLATE_T * 0.55), ring(i1, PLATE_T), ring(i2, PLATE_T), ring(i3, PLATE_T - PLATE_INSET)]
    c2 = np.array([[o[:, 0].mean() + 0.004, 0.0]])
    spine = ring(c2, PLATE_T - PLATE_INSET + 0.0006)
    vs, ts, mats, uvs = [], [], [], []
    at = 0
    for k in range(3):   # the outer bevel, the border's top, the inner bevel
        a, b = rings[k], rings[k + 1]
        vs += [a, b]
        i = np.arange(n)
        j = (i + 1) % n
        ts.append(np.concatenate([np.stack([at + i, at + j, at + n + j], 1), np.stack([at + i, at + n + j, at + n + i], 1)]))
        mats.append(np.full(2 * n, GOLD))
        uvs += [[o, i1, i2][k], [i1, i2, i3][k]]
        at += 2 * n
    vs += [rings[3], spine]
    i = np.arange(n)
    ts.append(np.stack([at + i, at + (i + 1) % n, np.full(n, at + n)], 1))
    mats.append(np.full(n, ENAMEL))
    uvs += [i3, c2]
    v = np.concatenate(vs)
    t = np.concatenate(ts)
    uv = np.concatenate(uvs)
    # face normals per strip, smoothed within a strip
    fn = np.cross(v[t[:, 1]] - v[t[:, 0]], v[t[:, 2]] - v[t[:, 0]])
    flip = fn[:, 1] < 0
    t[flip] = t[flip][:, ::-1]
    fn[flip] *= -1
    nn = np.zeros_like(v)
    for k in range(3):
        np.add.at(nn, t[:, k], fn)
    top = (v, norm(nn), t, uv, np.concatenate(mats))
    # the rim wall and the underside
    lo, hi = ring(o, 0.0), rings[0]
    under_c = ring(c2, 0.0)
    wv = np.concatenate([lo, hi, lo, under_c])
    j = (i + 1) % n
    wt = np.concatenate([np.stack([i, j, n + j], 1), np.stack([i, n + j, n + i], 1),
                         np.stack([2 * n + j, 2 * n + i, np.full(n, 3 * n)], 1)])
    fn = np.cross(wv[wt[:, 1]] - wv[wt[:, 0]], wv[wt[:, 2]] - wv[wt[:, 0]])
    centre = wv.mean(0)
    mid = wv[wt].mean(1)
    flip = (fn * (mid - centre - np.array([0, 0.004, 0]))).sum(1) < 0
    flip[2 * n * 1:] = fn[2 * n:, 1] > 0
    wt[flip] = wt[flip][:, ::-1]
    fn[flip] *= -1
    wn = np.zeros_like(wv)
    for k in range(3):
        np.add.at(wn, wt[:, k], fn)
    wuv = np.stack([wv[:, 0], wv[:, 2]], 1)
    return top, (wv, norm(wn), wt, wuv)


def plate_sd(p):
    """About how far a point is from the plate (for its shadow on what is under it)."""
    q = np.stack([p[:, 0], p[:, 2] - PLATE_Z], 1)
    x0, x1, x2 = PLATE_X
    inside = (q[:, 0] > x0) & (q[:, 0] < x2) & (np.abs(q[:, 1]) < PLATE_W[0] * np.clip((x2 - q[:, 0]) / (x2 - x1), 0, 1))
    return np.where(inside, np.abs(p[:, 1] - plate_height(q) - PLATE_T * 0.5) - PLATE_T * 0.5, 0.05)


def gear_sd(p):
    """How far a point is from the bands and the leather (for the shadows they cast)."""
    r = np.hypot(p[:, 1], p[:, 2])
    d = np.full(len(p), 1.0)
    for xc in (BAND_WRIST_X, BAND_ELBOW_X):
        qx = np.abs(p[:, 0] - xc) - BAND_HALF
        qr = np.abs(r - 0.5 * (BAND_RI + BAND_RO)) - 0.5 * (BAND_RO - BAND_RI)
        d = np.minimum(d, np.hypot(np.maximum(qx, 0), np.maximum(qr, 0)) + np.minimum(np.maximum(qx, qr), 0))
    inx = (p[:, 0] > LEATHER_X[0]) & (p[:, 0] < LEATHER_X[1])
    d = np.minimum(d, np.where(inx, np.abs(r - leather_radius(p[:, 0])), 1.0))
    return d
