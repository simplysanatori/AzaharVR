# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Pit's hand: the parts put together: geometry (mesh.py) with its skin weights, materials and
baked shade, in metres, in the bind pose. build() returns the list of parts."""
import numpy as np

import hand as H
import mesh as M
from hand import norm

# materials: a row each of the palette (pit_hand.h PitHandMaterial)
SKIN, WRAP, LEATHER, BAND, NAIL, THREAD, GOLD, STRAP = range(8)

P = H.BIND_P
R = H.BIND_R


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def seg_dist(p, a, b):
    ab = b - a
    t = np.clip(((p - a) @ ab) / (ab @ ab), 0, 1)
    return np.linalg.norm(p - a - t[:, None] * ab, axis=1)


# ---- skin weights ----
CHAINS = [(H.THUMB[0], [H.THUMB[1], H.THUMB[2]], H.THUMB[3], H.THUMB_R[1:])] + [
    (c[0], [c[1], c[2], c[3]], c[4], H.FINGER_R[c]) for c in H.FINGERS]


def chain_coords(p, joints, tip, radii):
    """Along a finger from its knuckle: the arc position of each point's nearest place on the
    bones (negative behind the knuckle), its distance from them, their radius there, and how
    far round to the back of the finger the point is (1 on the back, -1 on the palm's side)."""
    pts = [P[j] for j in joints] + [P[tip]]
    best_d = np.full(len(p), np.inf)
    best_s = np.zeros(len(p))
    best_r = np.zeros(len(p))
    best_back = np.zeros(len(p))
    arc = 0.0
    arcs = [0.0]
    for i in range(len(pts) - 1):
        a, b = pts[i], pts[i + 1]
        ln = np.linalg.norm(b - a)
        t = ((p - a) @ (b - a)) / (ln * ln)
        tc = np.clip(t, 0, 1)
        off = p - a - tc[:, None] * (b - a)
        d = np.linalg.norm(off, axis=1)
        s = arc + (np.minimum(t, 1.0) if i == 0 else tc) * ln
        r = radii[i] + (radii[i + 1] - radii[i]) * tc
        back = (off @ R[joints[i]][:, 1]) / np.maximum(d, 1e-9)
        better = d < best_d
        best_d = np.where(better, d, best_d)
        best_s = np.where(better, s, best_s)
        best_r = np.where(better, r, best_r)
        best_back = np.where(better, back, best_back)
        arc += ln
        arcs.append(arc)
    return best_s, best_d, best_r, arcs, best_back


def palm_weights(p):
    """The weights of what is not a free finger: the wrist, the metacarpals, the thumb's
    metacarpal (the ball of the thumb)."""
    w = np.zeros((len(p), H.NJ))
    a = smoothstep(0.010, 0.046, p[:, 0])
    g = np.zeros((len(p), 4))
    for i, c in enumerate(H.FINGERS):
        d = seg_dist(p * [1, 0.35, 1], P[c[0]] * [1, 0.35, 1], P[c[1]] * [1, 0.35, 1])
        g[:, i] = np.exp(-(d / 0.0085) ** 2)
    # the two nearest only
    order = np.argsort(-g, axis=1)
    keep = np.zeros_like(g)
    rows = np.arange(len(p))
    for k in range(2):
        keep[rows, order[:, k]] = g[rows, order[:, k]]
    keep /= keep.sum(1, keepdims=True)
    for i, c in enumerate(H.FINGERS):
        w[:, c[0]] = a * keep[:, i]
    w[:, H.WRIST] = 1 - a
    # the thumb's ball: near its metacarpal, on the palm's and the thumb's side of the hand
    c, k = P[H.THUMB[0]], P[H.THUMB[1]]
    t = smoothstep(0.040, 0.013, seg_dist(p, c, k))
    side = (p - (P[H.INDEX[0]] + np.array([0.012, -0.004, 0.006]))) @ norm([0.0, -0.62, 0.78])
    t *= smoothstep(-0.005, 0.006, side)
    t *= smoothstep(0.004, 0.022, p[:, 0])
    w *= (1 - t)[:, None]
    w[:, H.THUMB[0]] += t
    return w


def hand_weights(p):
    w = np.zeros((len(p), H.NJ))
    share = np.zeros((len(p), len(CHAINS)))
    parts = []
    for ci, (meta, joints, tip, radii) in enumerate(CHAINS):
        s, d, r, arcs, back = chain_coords(p, joints, tip, radii)
        # a joint's blend is wide on the back (a round knuckle), narrow inside (a crease)
        zone = 0.42 + 0.80 * (0.5 + 0.5 * back)
        u = np.clip(1.7 * (1 - (d - r) / 0.008), 0, 1) ** 2
        u *= smoothstep(-1.6 * radii[0], -0.6 * radii[0], s)
        share[:, ci] = u
        g = [smoothstep(arcs[i] - zone * radii[i], arcs[i] + zone * radii[i], s) for i in range(len(joints))]
        cw = []
        run = np.ones(len(p))
        for i in range(len(joints)):
            run = run * g[i]
            nxt = g[i + 1] if i + 1 < len(joints) else 0.0
            cw.append(run * (1 - nxt))
        parts.append((joints, cw))
    total = np.maximum(share.sum(1), 1.0)
    share /= total[:, None]
    finger = np.zeros(len(p))
    for ci, (joints, cw) in enumerate(parts):
        for j, x in zip(joints, cw):
            w[:, j] += share[:, ci] * x
            finger += share[:, ci] * x
    w += palm_weights(p) * (1 - finger)[:, None]
    # the forearm takes over behind the wrist
    f = smoothstep(0.006, -0.030, p[:, 0])
    w *= (1 - f)[:, None]
    w[:, H.FOREARM] += f
    return w


def prune(w, most=4):
    """At most four bones a vertex, as bytes that sum to 255."""
    idx = np.argsort(-w, axis=1)[:, :most]
    rows = np.arange(len(w))[:, None]
    val = w[rows, idx]
    val = np.where(val < 0.02, 0.0, val)
    val /= val.sum(1, keepdims=True)
    q = np.floor(val * 255 + 0.5).astype(int)
    q[:, 0] += 255 - q.sum(1)
    order = np.argsort(-q, axis=1, kind='stable')
    q = np.take_along_axis(q, order, 1)
    idx = np.take_along_axis(idx, order, 1)
    idx = np.where(q > 0, idx, idx[:, :1])
    return idx.astype(int), q


def rigid(n, joint):
    idx = np.full((n, 4), joint, int)
    q = np.zeros((n, 4), int)
    q[:, 0] = 255
    return idx, q


# ---- shade: baked soft shadow where the surface folds, and the warmth of the skin ----


def occlusion(field, p, n, steps=(0.004, 0.009, 0.016)):
    ao = np.zeros(len(p))
    for d in steps:
        ao += np.clip(field(p + n * d) / d, 0.0, 1.0)
    return ao / len(steps)


def skin_tint(p, n):
    """0..1: how rosy: the knuckles, the finger tips, the pads of the palm."""
    t = np.zeros(len(p))
    for c in H.FINGERS:
        for j, r, k in ((c[1], 0.011, 0.55), (c[2], 0.008, 0.5), (c[3], 0.007, 0.35)):
            up = R[j][:, 1]
            d = np.linalg.norm(p - (P[j] + up * 0.008), axis=1)
            t = np.maximum(t, k * np.exp(-(d / r) ** 2))
        d = np.linalg.norm(p - P[c[4]], axis=1)
        t = np.maximum(t, 0.6 * np.exp(-(d / 0.011) ** 2))
    d = np.linalg.norm(p - P[H.THUMB[3]], axis=1)
    t = np.maximum(t, 0.6 * np.exp(-(d / 0.012) ** 2))
    d = np.linalg.norm(p - (P[H.THUMB[2]] + R[H.THUMB[2]][:, 1] * 0.009), axis=1)
    t = np.maximum(t, 0.45 * np.exp(-(d / 0.009) ** 2))
    # the palm: its pads
    palm = np.clip(-n[:, 1], 0, 1)
    for c, r in ((np.array([0.040, -0.022, 0.026]), 0.020), (np.array([0.034, -0.02, -0.022]), 0.018),
                 (np.array([0.082, -0.012, -0.004]), 0.030)):
        t = np.maximum(t, 0.4 * palm * np.exp(-(np.linalg.norm(p - c, axis=1) / r) ** 2))
    return np.clip(t, 0, 1)


# ---- parts ----


def part(name, material, v, n, tris, bones, weights, shade=None, tint=None):
    k = len(v)
    return dict(name=name, material=material, v=v, n=n, tris=np.asarray(tris, int), bones=bones, weights=weights,
                shade=np.ones(k) if shade is None else shade, tint=np.zeros(k) if tint is None else tint)


def nail(chain_or_thumb, radii, thumb=False):
    """A finger nail: a thin closed shell lying on the back of the last phalanx."""
    if thumb:
        j_last, j_tip = H.THUMB[2], H.THUMB[3]
    else:
        j_last, j_tip = chain_or_thumb[3], chain_or_thumb[4]
    a, b = P[j_last], P[j_tip]
    axis = norm(b - a)
    ln = np.linalg.norm(b - a)
    side, up = R[j_last][:, 0], R[j_last][:, 1]
    r_tip = radii[-1]
    n_u, n_v = 4, 4
    top, bot = [], []
    for iv in range(n_v + 1):
        fv = iv / n_v
        s = ln * (0.40 + 0.47 * fv)
        # the outline: square with rounded corners at the root, a round free edge
        wide = 0.96 * (1 - 0.30 * fv ** 2.2) * (1 - 0.22 * (1 - fv) ** 3)
        for iu in range(n_u + 1):
            fu = iu / n_u * 2 - 1
            ang = np.radians(56) * wide * fu
            d = np.cos(ang) * up + np.sin(ang) * side
            o = a + axis * s
            lo_r, hi_r = 0.0, r_tip * 2.6
            for _ in range(22):
                mid = 0.5 * (lo_r + hi_r)
                if M.hand_field((o + d * mid)[None])[0] < 0:
                    lo_r = mid
                else:
                    hi_r = mid
            edge = max(abs(fu), abs(fv * 2 - 1))
            lift = 0.00055 * (1 - 0.55 * edge ** 3)
            top.append(o + d * (lo_r + lift))
            bot.append(o + d * (lo_r - 0.0009))
    top, bot = np.array(top), np.array(bot)
    cols = n_u + 1
    border = [(iv * cols + iu) for iv in range(n_v + 1) for iu in range(cols)
              if iv in (0, n_v) or iu in (0, n_u)]
    # the rim is shared: the bottom sheet uses the top's border vertices
    v = list(top)
    remap = {}
    for i in range(len(bot)):
        if i in border:
            remap[i] = i
            v[i] = 0.5 * (top[i] + bot[i]) + (top[i] - bot[i]) * 0.1
        else:
            remap[i] = len(v)
            v.append(bot[i])
    tris = []
    for iv in range(n_v):
        for iu in range(n_u):
            i0, i1, i2, i3 = iv * cols + iu, iv * cols + iu + 1, (iv + 1) * cols + iu + 1, (iv + 1) * cols + iu
            if i0 in border and i2 in border:  # a corner: no triangle wholly on the rim
                quad = (i1, i2, i3, i0)
            else:
                quad = (i0, i1, i2, i3)
            a, b, c, d = quad
            tris += [(a, b, c), (a, c, d)]
            tris += [(remap[a], remap[c], remap[b]), (remap[a], remap[d], remap[c])]
    v = np.array(v)
    tris = np.array(tris)
    # which way is out: the top sheet must face away from the bone
    c = v[tris[0]]
    if np.cross(c[1] - c[0], c[2] - c[0]) @ up < 0:
        tris = tris[:, ::-1]
    n = M.mesh_normals(v, tris)
    n = np.where(np.isfinite(n), n, up)
    return v, n, tris, j_last


def stitch(centre, normal, along, length, width, height):
    """One stitch of the seam: a small closed bar lying on the leather."""
    side = norm(np.cross(normal, along))
    hx, hy = along * length / 2, side * width / 2
    base = [centre - hx - hy, centre + hx - hy, centre + hx + hy, centre - hx + hy]
    inset = 0.72
    top = [centre + (q - centre) * inset + normal * height for q in base]
    low = [q - normal * 0.0012 for q in base]
    v = np.array(low + top)
    quads = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    tris = []
    for q in quads:
        tris += [(q[0], q[1], q[2]), (q[0], q[2], q[3])]
    tris = np.array(tris)
    cen = v.mean(0)
    for i, t in enumerate(tris):
        a, b, c = v[t]
        if np.cross(b - a, c - a) @ (a + b + c - 3 * cen) < 0:
            tris[i] = t[::-1]
    return v, tris


# The vambrace (metres along x; radii before the section's squash).
SQUASH = (0.90, 1.0)
SEG = 20
SLEEVE = [(-0.1400, 0.0398), (-0.1220, 0.0412), (-0.1000, 0.0410), (-0.0780, 0.0396), (-0.0560, 0.0378),
          (-0.0360, 0.0362), (-0.0200, 0.0352)]
BAND_ELBOW = (-0.1480, -0.1215, 0.0452, 0.0320)   # x0, x1, outer radius, inner radius
BAND_WRIST = (-0.0385, -0.0150, 0.0398, 0.0285)
STUB = [(H.ELBOW_X, 0.0345), (-0.1500, 0.0350), (-0.1380, 0.0352)]


def sleeve_radius(x):
    xs = [q[0] for q in SLEEVE]
    rs = [q[1] for q in SLEEVE]
    return np.interp(x, xs, rs)


def band_profile(x0, x1, ro, ri):
    lip, drop, bev = 0.0052, 0.0016, 0.0011
    return [(x0, ri), (x0, ro - bev), (x0 + bev, ro), (x0 + lip, ro), (x0 + lip + 0.0008, ro - drop),
            (x1 - lip - 0.0008, ro - drop), (x1 - lip, ro), (x1 - bev, ro), (x1, ro - bev), (x1, ri)]


def creased(name, material, v, tris, deg=38.0, shade=None):
    v2, n2, t2, src = M.split_creases(v, tris, deg)
    b, w = rigid(len(v2), H.FOREARM)
    return part(name, material, v2, n2, t2, b, w, None if shade is None else shade[src])


def joint_importance(p):
    """Where the mesh must stay fine: the fingers, most of all at their joints."""
    k = np.ones(len(p))
    for meta, joints, tip, radii in CHAINS:
        s, d, r, arcs, _ = chain_coords(p, joints, tip, radii)
        on = np.clip(1 - (d - r) / 0.004, 0, 1) * (s > -0.004)
        k = np.maximum(k, 1 + 1.2 * on)
        for i, a in enumerate(arcs[:-1]):
            k = np.maximum(k, 1 + 5.0 * on * np.exp(-((s - a) / (0.9 * radii[i])) ** 2))
    return k


def build(h=0.0030, skin_tris=3300, wrap_tris=700, log=print):
    parts = []
    # the hand's skin
    v, n, t = M.level_set(M.skin_field, (-0.022, -0.050, -0.078), (0.205, 0.040, 0.092), h, target=skin_tris,
                          importance=joint_importance)
    n = np.nan_to_num(n, nan=0.0) + np.where(np.isfinite(n).all(1, keepdims=True), 0.0, [0.0, 1.0, 0.0])
    b, w = prune(hand_weights(v))
    ao = occlusion(M.hand_field, v, n)
    # under the wrap (never seen) the skin sinks a little, so the cloth cannot let it through
    # when a knuckle bends
    v = v - n * (0.0024 * smoothstep(-0.0005, -0.0040, M.wrap_region(v)))[:, None]
    wrap_near = np.clip(M.wrap_field(v) / 0.004, 0, 1)  # a soft shadow under the wrap's edge
    shade = np.clip(0.50 + 0.50 * ao ** 0.75, 0, 1) * (0.86 + 0.14 * wrap_near)
    parts.append(part('skin', SKIN, v, n, t, b, w, shade, skin_tint(v, n)))
    log(f'skin {len(v)} v {len(t)} t {M.topology(t)}')
    # the wrap
    v, n, t = M.level_set(M.wrap_field, (-0.052, -0.040, -0.045), (0.130, 0.040, 0.050), 0.0022, target=wrap_tris,
                          importance=joint_importance)
    b, w = prune(hand_weights(v))
    ao = occlusion(lambda q: np.minimum(M.hand_field(q), M.wrap_field(q)), v, n)
    parts.append(part('wrap', WRAP, v, n, t, b, w, np.clip(0.55 + 0.45 * ao ** 0.75, 0, 1)))
    log(f'wrap {len(v)} v {len(t)} t {M.topology(t)}')
    # the nails
    for c in H.FINGERS:
        v, n, t, j = nail(c, H.FINGER_R[c])
        b, w = rigid(len(v), j)
        parts.append(part('nail', NAIL, v, n, t, b, w))
    v, n, t, j = nail(None, H.THUMB_R, thumb=True)
    b, w = rigid(len(v), j)
    parts.append(part('nail', NAIL, v, n, t, b, w))
    # the forearm's skin toward the elbow
    v, t = M.lathe(STUB, SEG, SQUASH)
    parts.append(creased('forearm', SKIN, v, t, 50.0))
    # the vambrace: the leather, its two bands, its seam
    v, t = M.lathe(SLEEVE, SEG, SQUASH)
    x = v[:, 0]
    shade = 0.80 + 0.20 * np.sqrt(np.clip(np.sin((x + 0.14) / 0.12 * np.pi), 0, 1))
    parts.append(creased('vambrace', LEATHER, v, t, 50.0, np.nan_to_num(shade, nan=0.8)))
    for bx in (BAND_ELBOW, BAND_WRIST):
        v, t = M.lathe(band_profile(*bx), SEG, SQUASH, closed_ends=False)
        r = np.linalg.norm(v[:, 1:] / np.array(SQUASH), axis=1)
        shade = np.where(r < bx[2] - 0.0012, 0.80, 1.0)
        parts.append(creased('band', BAND, v, t, 30.0, shade))
    # the seam along the top (the thumb's side): a braid of stitches
    xs = np.arange(-0.1130, -0.0440, 0.0062)
    sv, st = [], []
    for i, sx in enumerate(xs):
        for side in (-1, 1):
            ang = np.radians(90 + side * 7.5)
            r = sleeve_radius(sx)
            centre = np.array([sx, np.cos(ang) * r * SQUASH[0], np.sin(ang) * r * SQUASH[1]])
            normal = norm([0.0, np.cos(ang) / SQUASH[0], np.sin(ang) / SQUASH[1]])
            tang = norm(np.cross([1.0, 0, 0], normal))
            along = norm(np.array([1.0, 0, 0]) * 0.62 + tang * 0.78 * side)
            a, b = stitch(centre, normal, along, 0.0082, 0.0030, 0.0011)
            st.append(b + sum(len(q) for q in sv))
            sv.append(a)
    v, t = np.concatenate(sv), np.concatenate(st)
    parts.append(creased('seam', THREAD, v, t, 35.0))
    return parts


def merged(parts):
    """All parts as one mesh: v, n, tris, bones, weights, material, shade, tint, and each part's
    (name, first triangle, count)."""
    off = 0
    ranges = []
    tri_at = 0
    out = {k: [] for k in ('v', 'n', 'tris', 'bones', 'weights', 'material', 'shade', 'tint')}
    for p in parts:
        k = len(p['v'])
        out['v'].append(p['v'])
        out['n'].append(p['n'])
        out['tris'].append(p['tris'] + off)
        out['bones'].append(p['bones'])
        out['weights'].append(p['weights'])
        out['material'].append(np.full(k, p['material'], int))
        out['shade'].append(p['shade'])
        out['tint'].append(p['tint'])
        ranges.append((p['name'], tri_at, len(p['tris'])))
        tri_at += len(p['tris'])
        off += k
    return {k: np.concatenate(v) for k, v in out.items()}, ranges
