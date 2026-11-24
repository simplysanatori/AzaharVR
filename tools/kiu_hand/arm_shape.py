# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Pit's arm, drawn: the hand's skin. A coarse cage of quads (a tube from the forearm to the
finger webs, a finger pulled out of each front face, the thumb out of a side face) is
subdivided twice (Catmull-Clark) and laid on a smooth distance field built round the bind pose's
joints (arm_skel.py): the field gives the shape (full fingers, soft knuckles, the thumb's ball,
the webs), the cage gives clean loops round every joint. Then the skin weights, from where a
point is along and beside each bone. Nothing is read: all from the numbers here."""
import numpy as np

import arm_skel as S
from arm_skel import norm

P = S.BIND_P
R = S.BIND_R


# ---- distance fields ----
def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def smin(a, b, k):
    h = np.maximum(k - np.abs(a - b), 0.0) / k
    return np.minimum(a, b) - h * h * k * 0.25


def seg_param(p, a, b):
    ab = b - a
    return np.clip(((p - a) @ ab) / (ab @ ab), 0.0, 1.0)


def cone(p, a, b, ra, rb):
    t = seg_param(p, a, b)
    return np.linalg.norm(p - a - t[:, None] * (b - a), axis=1) - (ra + (rb - ra) * t)


def flat_cone(p, a, b, ra, rb, sy):
    s = np.array([1.0, 1.0 / sy, 1.0])
    return cone(p * s, a * s, b * s, ra, rb) * sy


def ball(p, c, r):
    return np.linalg.norm(p - c, axis=1) - r


def ellipsoid(p, c, r, rot=None):
    q = p - c
    if rot is not None:
        q = q @ rot
    k0 = np.linalg.norm(q / r, axis=1)
    k1 = np.linalg.norm(q / (r * r), axis=1)
    return k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)


def finger_field(p, chain):
    """A finger from its knuckle to its tip: full and smooth, the pads rounded toward the palm,
    the tip blunt. Returns the distance and how far along the first bone."""
    k, m, l, t = [P[j] for j in chain[1:]]
    r0, r1, r2, r3 = S.FINGER_R[chain]
    up = R[chain[2]][:, 1]
    tip_end = t - norm(t - l) * r3
    d = cone(p, k, m, r0 * 0.95, r1)
    d = smin(d, cone(p, m, l, r1, r2), 0.004)
    d = smin(d, cone(p, l, tip_end, r2, r3), 0.004)
    for a, b, ra, rb, f in ((k, m, r0, r1, 0.55), (m, l, r1, r2, 0.5)):   # the pads underneath
        c = a + (b - a) * f - up * 0.0010
        d = smin(d, ball(p, c, (ra + (rb - ra) * f) * 0.98), 0.006)
    pad = l + (tip_end - l) * 0.55 - R[chain[3]][:, 1] * 0.0012
    d = smin(d, ball(p, pad, r3 * 1.06), 0.006)
    return d, ((p - k) @ norm(m - k)) / np.linalg.norm(m - k)


def thumb_field(p):
    c, k, l, t = [P[j] for j in S.THUMB]
    r0, r1, r2, r3 = S.THUMB_R
    up = R[S.THUMB[2]][:, 1]
    tip_end = t - norm(t - l) * r3
    d = cone(p, k, l, r1, r2)
    d = smin(d, cone(p, l, tip_end, r2, r3), 0.004)
    d = smin(d, ball(p, k + (l - k) * 0.5 - R[S.THUMB[1]][:, 1] * 0.0012, (r1 + r2) * 0.49), 0.006)
    pad = l + (tip_end - l) * 0.55 - up * 0.0014
    d = smin(d, ball(p, pad, r3 * 1.06), 0.006)
    return d, ((p - k) @ norm(l - k)) / np.linalg.norm(l - k)


WRIST_HALF = (0.0245, 0.0300)   # the wrist's half thickness (y) and half width (z)
PALM_SY = 0.84


def palm_field(p):
    """The palm, the knuckles, the heel, the thumb's ball and the wrist."""
    d = None
    for chain in S.FINGERS:
        base, knuckle = P[chain[0]], P[chain[1]]
        rk = S.FINGER_R[chain][0] * 1.10
        c = flat_cone(p, base, knuckle, 0.0170, rk, PALM_SY)
        d = c if d is None else smin(d, c, 0.010)
    for chain in S.FINGERS:   # soft knuckles on the back
        k = P[chain[1]]
        d = smin(d, ball(p, k + np.array([-0.0020, 0.0030, 0.0]), S.FINGER_R[chain][0] * 0.92), 0.008)
    sy = WRIST_HALF[0] / WRIST_HALF[1]
    wrist = flat_cone(p, np.array([-0.090, 0.0, 0.0]), np.array([0.004, 0.0, 0.0]), WRIST_HALF[1], WRIST_HALF[1], sy)
    d = smin(d, wrist, 0.016)
    hyp = ellipsoid(p, np.array([0.034, -0.0075, -0.0300]), np.array([0.031, 0.0135, 0.0150]),
                    S.rot_y(np.radians(-10)))
    d = smin(d, hyp, 0.010)
    c, k = P[S.THUMB[0]], P[S.THUMB[1]]
    d = smin(d, cone(p, c + np.array([-0.006, 0.001, -0.004]), k, 0.0185, S.THUMB_R[0]), 0.012)
    # the web toward the index finger
    ik = P[S.INDEX[1]]
    web_a = k + (ik - k) * 0.30 + np.array([-0.008, 0.001, 0.0])
    web_b = P[S.INDEX[0]] + (ik - P[S.INDEX[0]]) * 0.60 + np.array([0, -0.002, 0.004])
    d = smin(d, flat_cone(p, web_a, web_b, 0.0090, 0.0110, 0.8), 0.010)
    return d


def hand_field(p):
    d = palm_field(p)
    for chain in S.FINGERS:
        f, t = finger_field(p, chain)
        d = smin(d, f, 0.0005 + 0.0080 * (1.0 - smoothstep(0.22, 0.80, t)))
    f, t = thumb_field(p)
    d = smin(d, f, 0.0005 + 0.0090 * (1.0 - smoothstep(0.15, 0.80, t)))
    return d


def gradient(field, p, e=2e-4):
    g = np.zeros_like(p)
    for i in range(3):
        d = np.zeros(3)
        d[i] = e
        g[:, i] = (field(p + d) - field(p - d)) / (2 * e)
    return g


# ---- the cage ----
TUBE, THUMB_T, CAP_T = 0, 1, 9          # face tags: the tube, digits 1..5 (thumb, index..little)
END_X = -0.046                           # the skin's hidden end, inside the vambrace
#          x       z centre  half width  top     bottom
STATIONS = [(END_X, 0.000, 0.0300, 0.0245, -0.0245),
            (-0.016, 0.000, 0.0300, 0.0245, -0.0245),
            (0.012, 0.002, 0.0350, 0.0230, -0.0260),
            (0.042, 0.001, 0.0470, 0.0200, -0.0250),
            (0.070, -0.001, 0.0530, 0.0170, -0.0190)]
WEB_T = 0.42    # how far along a finger's first bone the webs are


def ring_corners(c, rot, s, sy=None):
    sy = s if sy is None else sy
    x, y = rot[:, 0], rot[:, 1]
    return [c - x * s + y * sy, c + x * s + y * sy, c + x * s - y * sy, c - x * s - y * sy]


class Cage:
    def __init__(self):
        self.v = []
        self.f = []
        self.tag = []

    def vert(self, p):
        self.v.append(np.asarray(p, float))
        return len(self.v) - 1

    def face(self, quad, tag):
        self.f.append(list(quad))
        self.tag.append(tag)
        return len(self.f) - 1

    def extrude(self, fi, corners, tag):
        """Pulls face `fi` out to `corners` (the same winding; the best turn of them is
        taken). Returns the new end face."""
        old = self.f[fi]
        pts = [self.v[i] for i in old]
        best = min(range(4), key=lambda s: sum(np.linalg.norm(pts[i] - corners[(i + s) % 4]) for i in range(4)))
        new = [self.vert(corners[(i + best) % 4]) for i in range(4)]
        for i in range(4):
            j = (i + 1) % 4
            self.face([old[i], old[j], new[j], new[i]], tag)
        self.f[fi] = new
        self.tag[fi] = tag
        return fi


def build_cage():
    c = Cage()
    rings = []
    for x, zc, hw, top, bot in STATIONS:
        zs = [zc + hw * t for t in (-1.0, -0.5, 0.0, 0.5, 1.0)]
        ring = [c.vert((x, top * (0.72 if i in (0, 4) else 1.0), z)) for i, z in enumerate(zs)]
        ring += [c.vert((x, bot * (0.72 if i in (0, 4) else 1.0), z)) for i, z in reversed(list(enumerate(zs)))]
        rings.append(ring)
    # the front ring: on the webs between the fingers (columns from the little finger's side)
    order = [S.LITTLE, S.RING, S.MIDDLE, S.INDEX]
    pts = []
    for ch in order:
        k, m = P[ch[1]], P[ch[2]]
        pts.append((k + (m - k) * WEB_T, R[ch[1]], S.FINGER_R[ch][0]))
    cols = []
    for i in range(5):
        if i == 0:
            q, rot, r = pts[0]
            cols.append((q - rot[:, 0] * r * 0.9, r))
        elif i == 4:
            q, rot, r = pts[3]
            cols.append((q + rot[:, 0] * r * 0.9, r))
        else:
            cols.append((0.5 * (pts[i - 1][0] + pts[i][0]) - np.array([0.004, 0, 0]), 0.5 * (pts[i - 1][2] + pts[i][2])))
    ring = [c.vert(q + np.array([0, r * (0.7 if i in (0, 4) else 0.95), 0])) for i, (q, r) in enumerate(cols)]
    ring += [c.vert(q - np.array([0, r * (0.7 if i in (0, 4) else 0.95), 0])) for i, (q, r) in reversed(list(enumerate(cols)))]
    rings.append(ring)
    side = {}
    for k in range(len(rings) - 1):
        a, b = rings[k], rings[k + 1]
        for i in range(10):
            j = (i + 1) % 10
            side[(k, i)] = c.face([a[i], a[j], b[j], b[i]], TUBE)
    # the hidden end
    a = rings[0]
    for i in range(4):
        c.face([a[i + 1], a[i], a[9 - i], a[8 - i]], CAP_T)
    # the fingers, each out of its front face
    a = rings[-1]
    for i, ch in enumerate(order):
        tag = {id(S.INDEX): 2, id(S.MIDDLE): 3, id(S.RING): 4, id(S.LITTLE): 5}[id(ch)]
        f = c.face([a[i], a[i + 1], a[8 - i], a[9 - i]], tag)
        k, m, l, t = [P[j] for j in ch[1:]]
        r0, r1, r2, r3 = S.FINGER_R[ch]
        c.extrude(f, ring_corners(k + (m - k) * 0.80, R[ch[1]], r1 * 0.95), tag)
        c.extrude(f, ring_corners(m + (l - m) * 0.25, R[ch[2]], r1 * 0.95), tag)
        c.extrude(f, ring_corners(l - (l - m) * 0.22, R[ch[2]], r2 * 0.95), tag)
        c.extrude(f, ring_corners(l + (t - l) * 0.25, R[ch[3]], r2 * 0.95), tag)
        c.extrude(f, ring_corners(t - norm(t - l) * r3 * 0.40, R[ch[3]], r3 * 0.66), tag)
    # the thumb, out of the side face beside its ball
    f = side[(2, 4)]
    cm, k, l, t = [P[j] for j in S.THUMB]
    r0, r1, r2, r3 = S.THUMB_R
    c.extrude(f, ring_corners(cm + (k - cm) * 0.62, R[S.THUMB[0]], r0 * 1.05), THUMB_T)
    c.extrude(f, ring_corners(k + (l - k) * 0.30, R[S.THUMB[1]], r1 * 0.95), THUMB_T)
    c.extrude(f, ring_corners(l - (l - k) * 0.22, R[S.THUMB[1]], r2 * 0.95), THUMB_T)
    c.extrude(f, ring_corners(l + (t - l) * 0.25, R[S.THUMB[2]], r2 * 0.95), THUMB_T)
    c.extrude(f, ring_corners(t - norm(t - l) * r3 * 0.40, R[S.THUMB[2]], r3 * 0.66), THUMB_T)
    return np.array(c.v), np.array(c.f), np.array(c.tag)


def catmull_clark(v, f, tag):
    nv, nf = len(v), len(f)
    fp = v[f].mean(1)
    e = np.stack([f, np.roll(f, -1, 1)], 2).reshape(-1, 2)        # face i, corner c: edge c -> c+1
    es = np.sort(e, 1)
    uniq, inv = np.unique(es, axis=0, return_inverse=True)
    inv = inv.reshape(nf, 4)
    ne = len(uniq)
    fsum = np.zeros((ne, 3))
    np.add.at(fsum, inv.reshape(-1), np.repeat(fp, 4, 0))
    cnt = np.bincount(inv.reshape(-1), minlength=ne)
    assert (cnt == 2).all(), 'the cage is not closed'
    mid = v[uniq].mean(1)
    ep = (mid * 2 + fsum) / 4.0
    val = np.bincount(f.reshape(-1), minlength=nv).astype(float)
    q = np.zeros((nv, 3))
    np.add.at(q, f.reshape(-1), np.repeat(fp, 4, 0))
    q /= val[:, None]
    r = np.zeros((nv, 3))
    np.add.at(r, uniq[:, 0], mid)
    np.add.at(r, uniq[:, 1], mid)
    r /= val[:, None]
    vp = (q + 2 * r + (val[:, None] - 3) * v) / val[:, None]
    out_v = np.concatenate([vp, ep, fp])
    quads = []
    for c in range(4):
        quads.append(np.stack([f[:, c], nv + inv[:, c], nv + ne + np.arange(nf), nv + inv[:, (c - 1) % 4]], 1))
    out_f = np.stack(quads, 1).reshape(-1, 4)
    return out_v, out_f, np.repeat(tag, 4)


def neighbours(nv, f):
    e = np.stack([f, np.roll(f, -1, 1)], 2).reshape(-1, 2)
    e = np.unique(np.sort(e, 1), axis=0)
    return e


def lay_on(field, v, f, rounds=2, smooth=0.4, fixed=None):
    """Lays the vertices on the field's surface, evening them out along it."""
    e = neighbours(len(v), f)
    deg = np.bincount(e.reshape(-1), minlength=len(v)).astype(float)[:, None]

    def project(v, steps):
        for _ in range(steps):
            g = gradient(field, v)
            d = field(v)
            step = g * (d / np.maximum((g * g).sum(1), 1e-9))[:, None]
            far = np.linalg.norm(step, axis=1, keepdims=True)
            v = v - step * np.minimum(1.0, 0.003 / np.maximum(far, 1e-9))
        return v

    v = project(v, 10)
    for _ in range(rounds):
        s = np.zeros_like(v)
        np.add.at(s, e[:, 0], v[e[:, 1]])
        np.add.at(s, e[:, 1], v[e[:, 0]])
        move = s / deg - v
        n = norm(gradient(field, v))
        move -= n * (move * n).sum(1)[:, None]
        if fixed is not None:
            move[fixed] = 0
        v = project(v + move * smooth, 2)
    return v


def vertex_normals(v, tris):
    a, b, c = v[tris[:, 0]], v[tris[:, 1]], v[tris[:, 2]]
    fn = np.cross(b - a, c - a)
    n = np.zeros_like(v)
    for i in range(3):
        np.add.at(n, tris[:, i], fn)
    return norm(n)


# ---- the glove (cloth): where it is on the hand, as a signed distance along the skin ----
GLOVE_T = 0.0011            # its thickness over the skin
GLOVE_BACK_X = 0.040        # its edge across the back of the hand
GLOVE_PALM_X = 0.060        # and across the palm
LOOP_AT = (0.36, 0.0036)    # the finger loop: where along the index finger's first bone, its width


def glove_sd(p):
    """Negative inside the glove, metres along the skin (about). The glove covers the wrist,
    the palm and the back of the hand, runs to a soft V between the index and middle fingers,
    leaves the thumb and its ball free, and ends in a thin loop round the index finger."""
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    ang = np.abs(np.arctan2(z, y + 0.002))
    edge = GLOVE_BACK_X + (GLOVE_PALM_X - GLOVE_BACK_X) * smoothstep(np.radians(75), np.radians(125), ang)
    d = x - edge
    # the V: a tongue on the back of the hand toward the web between index and middle
    ik, im = P[S.INDEX[1]], P[S.INDEX[2]]
    mk, mm = P[S.MIDDLE[1]], P[S.MIDDLE[2]]
    apex = 0.5 * (ik + (im - ik) * WEB_T + mk + (mm - mk) * WEB_T)
    a2 = np.array([apex[0] + 0.004, apex[2]])
    b2 = np.array([GLOVE_BACK_X - 0.004, apex[2] - 0.004])
    q = np.stack([x, z], 1)
    t = np.clip(((q - a2) @ (b2 - a2)) / ((b2 - a2) @ (b2 - a2)), 0, 1)
    half = 0.0030 + 0.0170 * t ** 1.3
    tongue = np.linalg.norm(q - a2 - t[:, None] * (b2 - a2), axis=1) - half
    tongue = np.where(y > -0.004, tongue, 1.0)
    d = smin(d, tongue, 0.006)
    # the thumb and its ball stay bare
    c, k, l, tt = [P[j] for j in S.THUMB]
    free = cone(p, c + (k - c) * 0.30, tt, 0.0235, 0.0200)
    free = np.minimum(free, cone(p, c + (k - c) * 0.30 + np.array([0.0, -0.012, 0.0]), k + np.array([0.004, -0.012, -0.004]), 0.0210, 0.0160))
    d = np.maximum(d, -free)
    # the loop round the index finger
    s = ((p - ik) @ norm(im - ik)) / np.linalg.norm(im - ik)
    near = cone(p, ik, im, S.FINGER_R[S.INDEX][0] + 0.004, S.FINGER_R[S.INDEX][1] + 0.004)
    loop = np.abs(s - LOOP_AT[0]) * np.linalg.norm(im - ik) - LOOP_AT[1] * 0.5
    loop = np.where(near < 0, loop, 1.0)
    return np.minimum(d, loop)


# ---- skin weights ----
def chain_weights(p, joints, radii, ups):
    """How far past each joint of a chain a point is (0 before, 1 after): the bend is short on
    the back (a knuckle stays a knuckle) and long underneath (a fold, not a pinch)."""
    out = []
    for i in range(len(joints) - 1):
        d = norm(joints[i + 1] - joints[i])
        if i > 0:
            d = norm(d + norm(joints[i] - joints[i - 1]))
        s = (p - joints[i]) @ d
        h = np.clip(((p - joints[i]) @ ups[i]) / radii[i], -1.0, 1.0)
        b = radii[i] * (0.62 - 0.24 * h)
        out.append(smoothstep(-b, b, s))
    return out


def hand_weights(p, vtag):
    """(N, NJ) weights. `vtag`: 0 for a vertex of the tube or on a border, else its digit."""
    n = len(p)
    w = np.zeros((n, S.NJ))
    x = p[:, 0]
    fore = smoothstep(0.008, -0.028, x)
    # the thumb's share
    cm, k, l, t = [P[j] for j in S.THUMB]
    s = seg_param(p, cm, k)
    dm = np.linalg.norm(p - cm - s[:, None] * (k - cm), axis=1)
    tau = (1.0 - smoothstep(0.017, 0.036, dm)) * smoothstep(-0.004, 0.016, x)
    is_thumb = vtag == THUMB_T
    tau = np.where(is_thumb, np.maximum(tau, smoothstep(0.35, 0.85, ((p - cm) @ norm(k - cm)) / np.linalg.norm(k - cm))), tau)
    tau = np.where((vtag > THUMB_T), 0.0, tau)
    v1, v2 = chain_weights(p, [k, l, t], S.THUMB_R[1:], [R[S.THUMB[1]][:, 1], R[S.THUMB[2]][:, 1]])
    w[:, S.THUMB[0]] += tau * (1 - v1)
    w[:, S.THUMB[1]] += tau * v1 * (1 - v2)
    w[:, S.THUMB[2]] += tau * v1 * v2
    # the fingers and the palm
    aff = np.zeros((n, 4))
    for fi, ch in enumerate(S.FINGERS):
        a, b = P[ch[1]], P[ch[2]]
        d = norm(b - a)
        q = (p - a) - ((p - a) @ d)[:, None] * d
        aff[:, fi] = np.exp(-((q[:, 2] / 0.0105) ** 2))
        own = vtag == fi + 2
        aff[own] = 0
        aff[own, fi] = 1
    aff /= aff.sum(1, keepdims=True)
    gamma = smoothstep(0.015, 0.062, x)
    rest = 1.0 - tau
    for fi, ch in enumerate(S.FINGERS):
        js = [P[j] for j in ch[1:]]
        u1, u2, u3 = chain_weights(p, js, S.FINGER_R[ch], [R[ch[1]][:, 1], R[ch[2]][:, 1], R[ch[3]][:, 1]])
        a = aff[:, fi] * rest
        w[:, ch[0]] += a * (1 - u1) * gamma
        w[:, S.WRIST] += a * (1 - u1) * (1 - gamma)
        w[:, ch[1]] += a * u1 * (1 - u2)
        w[:, ch[2]] += a * u1 * u2 * (1 - u3)
        w[:, ch[3]] += a * u1 * u2 * u3
    w *= (1 - fore)[:, None]
    w[:, S.FOREARM] += fore
    return w


def prune(w, most=4):
    """The four heaviest bones of each vertex: (N, 4) bones, (N, 4) weights summing to 1, in
    falling order."""
    idx = np.argsort(-w, axis=1, kind='stable')[:, :most]
    ww = np.take_along_axis(w, idx, 1)
    ww = np.where(ww < 0.004, 0.0, ww)
    ww /= ww.sum(1, keepdims=True)
    idx = np.where(ww > 0, idx, idx[:, :1])
    return idx, ww


def build_hand(levels=2, log=print):
    """The hand's skin: vertices, triangles, each triangle's tag (0 the tube, 1..5 a digit),
    each vertex's tag, normals."""
    v, f, tag = build_cage()
    for _ in range(levels):
        v, f, tag = catmull_clark(v, f, tag)
    cap = tag == CAP_T
    fixed = np.zeros(len(v), bool)
    fixed[np.unique(f[cap])] = True
    v = lay_on(hand_field, v, f, fixed=None)
    v[fixed, 0] = np.minimum(v[fixed, 0], END_X)
    f, tag = f[~cap], tag[~cap]
    # vertex tags: a digit's own, or 0 on the tube and on any border
    vt = np.full(len(v), -1)
    for q, t in zip(f, tag):
        for i in q:
            vt[i] = t if vt[i] in (-1, t) else 0
    used = np.unique(f)
    remap = np.full(len(v), -1)
    remap[used] = np.arange(len(used))
    v, vt, f = v[used], vt[used], remap[f]
    # triangles: the shorter diagonal
    d02 = np.linalg.norm(v[f[:, 0]] - v[f[:, 2]], axis=1)
    d13 = np.linalg.norm(v[f[:, 1]] - v[f[:, 3]], axis=1)
    t1 = np.where((d02 <= d13)[:, None], f[:, [0, 1, 2]], f[:, [0, 1, 3]])
    t2 = np.where((d02 <= d13)[:, None], f[:, [0, 2, 3]], f[:, [1, 2, 3]])
    tris = np.stack([t1, t2], 1).reshape(-1, 3)
    ttag = np.repeat(tag, 2)
    # the glove stands a little off the skin
    n = vertex_normals(v, tris)
    g = smoothstep(0.0012, -0.0012, glove_sd(v))
    v = v + n * (GLOVE_T * g)[:, None]
    n = vertex_normals(v, tris)
    log(f'hand: {len(v)} vertices, {len(tris)} triangles')
    return v, tris, ttag, vt, n
