# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Pit's hand: the surfaces. The hand's skin and the cloth wrap are level sets of smooth
distance fields built round the bind pose's joints (hand.py), meshed with surface nets; the
nails, the vambrace, its bands and its seam are small built shapes. Every part is closed."""
import numpy as np

import hand as H
from hand import norm

# ---- distance fields ----


def smin(a, b, k):
    h = np.maximum(k - np.abs(a - b), 0.0) / k
    return np.minimum(a, b) - h * h * k * 0.25


def smax(a, b, k):
    return -smin(-a, -b, k)


def seg_param(p, a, b):
    ab = b - a
    return np.clip(((p - a) @ ab) / (ab @ ab), 0.0, 1.0)


def cone(p, a, b, ra, rb):
    """A rounded cone from a (radius ra) to b (radius rb)."""
    t = seg_param(p, a, b)
    return np.linalg.norm(p - a - t[:, None] * (b - a), axis=1) - (ra + (rb - ra) * t)


def flat_cone(p, a, b, ra, rb, sy):
    """A rounded cone flattened to `sy` of its thickness along y."""
    s = np.array([1.0, 1.0 / sy, 1.0])
    return cone(p * s, a * s, b * s, ra, rb) * sy


def ellipsoid(p, c, r, rot=None):
    q = p - c
    if rot is not None:
        q = q @ rot
    k0 = np.linalg.norm(q / r, axis=1)
    k1 = np.linalg.norm(q / (r * r), axis=1)
    return k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)


P = H.BIND_P
R = H.BIND_R


def finger_field(p, chain, radii):
    """One finger from its knuckle to its tip: three phalanges, the joints a little full, the
    tip's pad rounded toward the palm."""
    k, m, l, t = [P[j] for j in chain[1:]]
    r0, r1, r2, r3 = radii
    tip_dir = norm(t - l)
    tip_end = t - tip_dir * r3
    d = cone(p, k, m, r0, r1)
    d = smin(d, cone(p, m, l, r1, r2), 0.0015)
    d = smin(d, cone(p, l, tip_end, r2, r3), 0.0015)
    # the joints: a soft fullness on the back (knuckles) rather than a plain tube
    up_m = R[chain[2]][:, 1]
    up_l = R[chain[3]][:, 1]
    d = smin(d, np.linalg.norm(p - (m + up_m * 0.0009), axis=1) - r1 * 1.0, 0.003)
    d = smin(d, np.linalg.norm(p - (l + up_l * 0.0008), axis=1) - r2 * 0.99, 0.003)
    # the pad of the finger tip
    pad = l + (tip_end - l) * 0.62 - R[chain[3]][:, 1] * 0.0011
    d = smin(d, np.linalg.norm(p - pad, axis=1) - r3 * 1.03, 0.004)
    return d


def thumb_field(p):
    c, k, l, t = [P[j] for j in H.THUMB]
    r0, r1, r2, r3 = H.THUMB_R
    tip_dir = norm(t - l)
    tip_end = t - tip_dir * r3
    d = cone(p, k, l, r1, r2)
    d = smin(d, cone(p, l, tip_end, r2, r3), 0.002)
    d = smin(d, np.linalg.norm(p - (l + R[H.THUMB[2]][:, 1] * 0.0008), axis=1) - r2 * 1.03, 0.003)
    pad = l + (tip_end - l) * 0.6 - R[H.THUMB[2]][:, 1] * 0.0014
    d = smin(d, np.linalg.norm(p - pad, axis=1) - r3 * 1.06, 0.004)
    return d


PALM_SY = 0.80


def palm_field(p):
    """The palm, the heel, the thenar and the wrist: everything but the free fingers."""
    d = None
    for chain in H.FINGERS:
        base, knuckle = P[chain[0]], P[chain[1]]
        rk = H.FINGER_R[chain][0] * 1.16
        rb = 0.0150 if chain is not H.LITTLE else 0.0138
        c = flat_cone(p, base, knuckle, rb, rk, PALM_SY)
        d = c if d is None else smin(d, c, 0.008)
    # the knuckles on the back
    for chain in H.FINGERS:
        k = P[chain[1]]
        d = smin(d, np.linalg.norm(p - (k + np.array([-0.003, 0.0022, 0.0])), axis=1) -
                 H.FINGER_R[chain][0] * 0.90, 0.007)
    # the wrist and the heel of the hand
    wrist = flat_cone(p, np.array([-0.075, 0.0, 0.0]), np.array([0.006, 0.0, 0.0]), 0.0262, 0.0268, 0.76)
    d = smin(d, wrist, 0.016)
    # the heel on the little finger's side, and the thenar: the thumb's ball
    hyp = ellipsoid(p, np.array([0.036, -0.0085, -0.021]), np.array([0.027, 0.0105, 0.0135]),
                    H.rot_y(np.radians(-14)))
    d = smin(d, hyp, 0.010)
    c, k = P[H.THUMB[0]], P[H.THUMB[1]]
    d = smin(d, cone(p, c + np.array([-0.004, 0.0, -0.002]), k, 0.0150, H.THUMB_R[0] * 0.98), 0.012)
    # the web toward the index finger
    web_a = k + (P[H.INDEX[1]] - k) * 0.25 + np.array([-0.006, 0.002, 0.0])
    web_b = P[H.INDEX[0]] + (P[H.INDEX[1]] - P[H.INDEX[0]]) * 0.62 + np.array([0, -0.002, 0.004])
    d = smin(d, flat_cone(p, web_a, web_b, 0.0075, 0.0095, 0.8), 0.010)
    return d


def hand_field(p):
    """The whole hand's skin (not cut at the wrist)."""
    d = thumb_field(p)
    for chain in H.FINGERS:
        d = np.minimum(d, finger_field(p, chain, H.FINGER_R[chain]))
    return smin(palm_field(p), d, 0.0052)


# The cloth wrap (a hand guard): round the wrist, a tongue up the back of the hand to the middle
# finger's knuckle, and a loop round that finger.
SKIN_CUT_X = -0.010  # the skin part ends here, inside the wrap
WRAP_T = 0.0023
WRAP_END_X = -0.046
WRAP_PALM_X = 0.013
LOOP_S = (0.0215, 0.0295)


def wrap_region(p):
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    # the cuff: all round, its edge a little further up the hand on the back
    edge = WRAP_PALM_X + 0.004 * np.clip(y / 0.012, -1, 1)
    cuff = x - edge
    # the tongue on the back
    mk = P[H.MIDDLE[1]]
    mi = P[H.MIDDLE[2]]
    t = np.clip((x - 0.004) / (mk[0] - 0.004), 0.0, 1.2)
    zc = mk[2] * t
    half = 0.0300 + (0.0062 - 0.0300) * np.clip(t, 0, 1) ** 0.85
    tongue = np.maximum(np.abs(z - zc) - half, 0.0035 - y)
    tongue = np.maximum(tongue, x - (mk[0] + 0.006))
    # the strap and the loop on the middle finger
    axis = norm(mi - mk)
    s = (p - mk) @ axis
    radial = np.linalg.norm(p - mk - np.outer(s, axis), axis=1)
    up = R[H.MIDDLE[1]][:, 1]
    strap = np.maximum.reduce([np.abs((p - mk) @ R[H.MIDDLE[1]][:, 0]) - 0.0056,
                               -0.004 - s, s - LOOP_S[1], 0.002 - (p - mk) @ up])
    loop = np.maximum.reduce([LOOP_S[0] - s, s - LOOP_S[1], radial - 0.0125])
    return np.minimum.reduce([cuff, tongue, strap, loop])


def skin_field(p):
    return np.maximum(hand_field(p), SKIN_CUT_X - p[:, 0])


def wrap_field(p):
    d = hand_field(p) - WRAP_T
    d = smax(d, wrap_region(p), 0.0012)
    return np.maximum(d, WRAP_END_X - p[:, 0])


# ---- surface nets ----


def surface_nets(field, lo, hi, h):
    """A closed triangle mesh of field = 0 inside the box. Returns vertices and triangles
    (wound counter-clockwise seen from outside)."""
    n = np.ceil((np.asarray(hi) - lo) / h).astype(int) + 1
    ax = [lo[i] + np.arange(n[i]) * h for i in range(3)]
    g = np.stack(np.meshgrid(*ax, indexing='ij'), -1)
    f = field(g.reshape(-1, 3)).reshape(n)
    inside = f < 0
    # cells with a crossing
    c = inside[:-1, :-1, :-1].astype(int)
    cnt = np.zeros_like(c)
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                cnt += inside[dx:n[0] - 1 + dx, dy:n[1] - 1 + dy, dz:n[2] - 1 + dz]
    cell = (cnt > 0) & (cnt < 8)
    index = -np.ones(cell.shape, int)
    index[cell] = np.arange(cell.sum())
    # each cell's vertex: the mean of its edges' crossings
    acc = np.zeros(cell.shape + (3,))
    num = np.zeros(cell.shape)
    for axis in range(3):
        sl_a = [slice(None)] * 3
        sl_b = [slice(None)] * 3
        sl_a[axis] = slice(0, -1)
        sl_b[axis] = slice(1, None)
        fa, fb = f[tuple(sl_a)], f[tuple(sl_b)]
        cross = (fa < 0) != (fb < 0)
        t = np.where(cross, fa / np.where(cross, fa - fb, 1.0), 0.0)
        pt = g[tuple(sl_a)] + t[..., None] * (g[tuple(sl_b)] - g[tuple(sl_a)])
        o1, o2 = [a for a in range(3) if a != axis]
        for d1 in (0, 1):
            for d2 in (0, 1):
                # the edge at node (i, j, k) along axis belongs to the cells at -d1, -d2
                src = [slice(None)] * 3
                dst = [slice(None)] * 3
                src[o1] = slice(d1, n[o1] - 1 + d1)
                src[o2] = slice(d2, n[o2] - 1 + d2)
                m = cross[tuple(src)]
                acc[m] += pt[tuple(src)][m]
                num[m] += 1
    verts = acc[cell] / num[cell][:, None]
    # a quad for every crossed grid edge, between the four cells round it
    quads = []
    for axis in range(3):
        o1, o2 = [a for a in range(3) if a != axis]
        sl_a = [slice(None)] * 3
        sl_b = [slice(None)] * 3
        sl_a[axis] = slice(0, -1)
        sl_b[axis] = slice(1, None)
        ia, ib = inside[tuple(sl_a)], inside[tuple(sl_b)]
        cross = ia != ib
        idx = np.argwhere(cross)
        idx = idx[(idx[:, o1] > 0) & (idx[:, o2] > 0) & (idx[:, o1] < n[o1] - 1) & (idx[:, o2] < n[o2] - 1)]
        def cellat(d1, d2):
            q = idx.copy()
            q[:, o1] -= d1
            q[:, o2] -= d2
            return index[q[:, 0], q[:, 1], q[:, 2]]
        a, b, cc, d = cellat(1, 1), cellat(0, 1), cellat(0, 0), cellat(1, 0)
        q = np.stack([a, b, cc, d], 1)
        assert (q >= 0).all()
        # orientation: o1 x o2 is +axis for axis 0 and 2, -axis for axis 1
        flip = ia[idx[:, 0], idx[:, 1], idx[:, 2]]  # inside at the low node: the normal is +axis
        if axis == 1:
            flip = ~flip
        q[~flip] = q[~flip][:, ::-1]
        quads.append(q)
    quads = np.concatenate(quads)
    return verts, quads


def gradient(field, p, e=2e-4):
    g = np.zeros_like(p)
    for i in range(3):
        d = np.zeros(3)
        d[i] = e
        g[:, i] = field(p + d) - field(p - d)
    return g / (2 * e)


def relax(field, v, quads, rounds=6, smooth=0.5):
    """Evens out the net's vertices along the surface and puts them back on it."""
    nb_sum = np.zeros_like(v)
    for _ in range(rounds):
        nb_sum[:] = 0
        cnt = np.zeros(len(v))
        for i in range(4):
            a, b = quads[:, i], quads[:, (i + 1) % 4]
            np.add.at(nb_sum, a, v[b])
            np.add.at(cnt, a, 1)
        v = v + smooth * (nb_sum / cnt[:, None] - v)
        for _ in range(3):
            g = gradient(field, v)
            v = v - g * (field(v) / np.maximum((g * g).sum(1), 1e-12))[:, None]
    return v


def triangulate(v, quads):
    a, b, c, d = v[quads[:, 0]], v[quads[:, 1]], v[quads[:, 2]], v[quads[:, 3]]
    ac = np.linalg.norm(a - c, axis=1) <= np.linalg.norm(b - d, axis=1)
    t1 = np.where(ac[:, None], quads[:, [0, 1, 2]], quads[:, [0, 1, 3]])
    t2 = np.where(ac[:, None], quads[:, [0, 2, 3]], quads[:, [1, 2, 3]])
    return np.concatenate([t1, t2])


def clean(v, tris, tol=1e-7):
    """Drops degenerate triangles and unused vertices (welding nothing: surface nets shares
    vertices already)."""
    a, b, c = v[tris[:, 0]], v[tris[:, 1]], v[tris[:, 2]]
    area = np.linalg.norm(np.cross(b - a, c - a), axis=1)
    tris = tris[area > tol * tol]
    used = np.unique(tris)
    remap = -np.ones(len(v), int)
    remap[used] = np.arange(len(used))
    return v[used], remap[tris]


def topology(tris):
    """(closed, euler characteristic, components): closed when every edge has two triangles
    that run it in opposite directions."""
    e = np.concatenate([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [2, 0]]])
    key = np.sort(e, 1)
    uniq, inv, counts = np.unique(key, axis=0, return_inverse=True, return_counts=True)
    closed = bool((counts == 2).all())
    if closed:
        sign = np.where(e[:, 0] < e[:, 1], 1, -1)
        tot = np.zeros(len(uniq), int)
        np.add.at(tot, inv.reshape(-1), sign)
        closed = bool((tot == 0).all())
    nv = len(np.unique(tris))
    # components by union-find
    parent = np.arange(tris.max() + 1)
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for a, b in uniq:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb
    comps = len({find(x) for x in np.unique(tris)})
    return closed, nv - len(uniq) + len(tris), comps


def level_set(field, lo, hi, h, rounds=6, target=None, importance=None):
    # A net can pinch (an edge with four triangles) where two sheets pass through one cell:
    # the grid is moved a little until it does not.
    for shift in range(8):
        lo_s = np.asarray(lo, float) - h * 0.137 * shift
        v, q = surface_nets(field, lo_s, np.asarray(hi, float), h)
        if topology(triangulate(v, q))[0]:
            break
    else:
        raise RuntimeError('surface net not closed')
    v = relax(field, v, q, rounds)
    t = triangulate(v, q)
    v, t = clean(v, t)
    if target is not None and len(t) > target:
        v, t = decimate(v, t, target, projector(field), importance=None if importance is None else importance(v))
    n = norm(gradient(field, v))
    return v, n, t


# ---- built shapes ----


def lathe(profile, segments, squash=(1.0, 1.0), closed_ends=True, centre=(0.0, 0.0)):
    """A surface of revolution round the x axis from `profile` [(x, radius)] (elbow to wrist or
    a closed loop), its section an ellipse `squash` (y, z). Returns v, n, tris."""
    prof = np.asarray(profile, float)
    ang = np.arange(segments) * 2 * np.pi / segments
    ring = np.stack([np.zeros(segments), np.cos(ang) * squash[0], np.sin(ang) * squash[1]], 1)
    v = []
    for x, r in prof:
        q = ring * r
        q[:, 0] = x
        q[:, 1] += centre[0]
        q[:, 2] += centre[1]
        v.append(q)
    v = np.concatenate(v)
    tris = []
    rows = len(prof)
    loop = not closed_ends
    for i in range(rows if loop else rows - 1):
        a0 = i * segments
        a1 = ((i + 1) % rows) * segments
        for s in range(segments):
            s1 = (s + 1) % segments
            # x grows along the profile: outward normals need (a0+s, a1+s, a1+s1)
            tris.append((a0 + s, a1 + s1, a1 + s))
            tris.append((a0 + s, a0 + s1, a1 + s1))
    if closed_ends:
        c0, c1 = len(v), len(v) + 1
        v = np.concatenate([v, [[prof[0, 0], centre[0], centre[1]], [prof[-1, 0], centre[0], centre[1]]]])
        last = (rows - 1) * segments
        for s in range(segments):
            s1 = (s + 1) % segments
            tris.append((c0, s1, s))
            tris.append((c1, last + s, last + s1))
    tris = np.array(tris)
    return v, tris


def mesh_normals(v, tris):
    """Area weighted vertex normals."""
    fn = np.cross(v[tris[:, 1]] - v[tris[:, 0]], v[tris[:, 2]] - v[tris[:, 0]])
    n = np.zeros_like(v)
    for i in range(3):
        np.add.at(n, tris[:, i], fn)
    return norm(n)


def split_creases(v, tris, deg=40.0):
    """Gives a hard edge its own vertices on each side: corners whose face normals differ by
    more than `deg` no longer share a vertex. Returns v, n, tris, and each new vertex's
    original (so the shape stays closed when welded by position)."""
    fn = norm(np.cross(v[tris[:, 1]] - v[tris[:, 0]], v[tris[:, 2]] - v[tris[:, 0]]))
    cos = np.cos(np.radians(deg))
    faces_of = [[] for _ in range(len(v))]
    for fi, t in enumerate(tris):
        for c in range(3):
            faces_of[t[c]].append((fi, c))
    nv, nn, src = [], [], []
    out = tris.copy()
    for vi, fl in enumerate(faces_of):
        groups = []
        for fi, c in fl:
            for g in groups:
                if fn[g[0][0]] @ fn[fi] > cos:
                    g.append((fi, c))
                    break
            else:
                groups.append([(fi, c)])
        for g in groups:
            idx = len(nv)
            nv.append(v[vi])
            m = np.zeros(3)
            for fi, c in g:
                a, b, cc = v[tris[fi]]
                m += np.cross(b - a, cc - a)
                out[fi, c] = idx
            nn.append(m / max(np.linalg.norm(m), 1e-12))
            src.append(vi)
    return np.array(nv), np.array(nn), out, np.array(src)


def decimate(v, tris, target, project=None, max_err=None, importance=None):
    """Quadric edge collapse down to `target` triangles, keeping the surface closed and
    manifold (the link condition) and never turning a triangle over. `project` puts a new
    vertex back on the surface."""
    import heapq
    v = v.copy()
    tris = [tuple(int(i) for i in t) for t in tris]
    alive = [True] * len(tris)
    vt = [set() for _ in range(len(v))]
    for fi, t in enumerate(tris):
        for i in t:
            vt[i].add(fi)
    Q = np.zeros((len(v), 4, 4))
    for t in tris:
        a, b, c = v[list(t)]
        n = np.cross(b - a, c - a)
        area = np.linalg.norm(n)
        if area < 1e-18:
            continue
        n /= area
        p = np.append(n, -n @ a)
        k = np.outer(p, p) * area
        for i in t:
            Q[i] += k

    def neighbours(i):
        s = set()
        for fi in vt[i]:
            s.update(tris[fi])
        s.discard(i)
        return s

    def cost(i, j):
        q = Q[i] + Q[j]
        best = None
        for cand in (0.5 * (v[i] + v[j]), v[i], v[j]):
            h = np.append(cand, 1.0)
            e = float(h @ q @ h)
            if importance is not None:
                e *= max(imp[i], imp[j])
            if best is None or e < best[0]:
                best = (e, cand)
        return best

    imp = None if importance is None else list(importance)
    stamp = [0] * len(v)
    heap = []

    def push(i, j):
        if i > j:
            i, j = j, i
        e, _ = cost(i, j)
        heapq.heappush(heap, (e, i, j, stamp[i], stamp[j]))

    seen = set()
    for t in tris:
        for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            key = (min(a, b), max(a, b))
            if key not in seen:
                seen.add(key)
                push(*key)
    count = len(tris)
    dead_v = [False] * len(v)
    while count > target and heap:
        e, i, j, si, sj = heapq.heappop(heap)
        if dead_v[i] or dead_v[j] or stamp[i] != si or stamp[j] != sj:
            continue
        if max_err is not None and e > max_err:
            break
        ni, nj = neighbours(i), neighbours(j)
        if j not in ni:
            continue
        if len(ni & nj) != 2:
            continue
        _, pos = cost(i, j)
        # no triangle may turn over or collapse
        ok = True
        shared = vt[i] & vt[j]
        for fi in (vt[i] | vt[j]) - shared:
            t = tris[fi]
            old = [v[k] for k in t]
            new = [pos if k in (i, j) else v[k] for k in t]
            n0 = np.cross(old[1] - old[0], old[2] - old[0])
            n1 = np.cross(new[1] - new[0], new[2] - new[0])
            l1 = np.linalg.norm(n1)
            if l1 < 1e-12 or n0 @ n1 < 0.2 * np.linalg.norm(n0) * l1:
                ok = False
                break
            # no slivers
            el = max(np.linalg.norm(new[1] - new[0]), np.linalg.norm(new[2] - new[1]), np.linalg.norm(new[0] - new[2]))
            if l1 < 0.12 * el * el:
                ok = False
                break
        if not ok:
            continue
        # collapse j into i
        for fi in shared:
            alive[fi] = False
            for k in tris[fi]:
                vt[k].discard(fi)
            count -= 1
        for fi in list(vt[j]):
            t = tris[fi]
            tris[fi] = tuple(i if k == j else k for k in t)
            vt[i].add(fi)
        vt[j] = set()
        dead_v[j] = True
        v[i] = pos
        Q[i] = Q[i] + Q[j]
        stamp[i] += 1
        for k in neighbours(i):
            push(i, k)
    out = np.array([t for fi, t in enumerate(tris) if alive[fi]])
    v, out = clean(v, out)
    if project is not None:
        v = project(v)
    return v, out


def projector(field, steps=4):
    def f(p):
        for _ in range(steps):
            g = gradient(field, p)
            p = p - g * (field(p) / np.maximum((g * g).sum(1), 1e-12))[:, None]
        return p
    return f
