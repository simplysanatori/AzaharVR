# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Pit's arm, drawn: its two images, painted by this script. Every texel of the atlas knows the
point of the arm it covers (the triangles are rasterised in the atlas), and its colour, its
relief and its shade are functions of that point: skin with rosy tips and knuckles and warm
creases, the navy glove with its raised edge, leather with its grain and a tooled scroll beside
each band, gold, the plate's red, and the ambient occlusion (short reach, from the distance
fields the shapes were made from). The colours are typed here; they were chosen once by
looking at the owner's reference pictures. No picture is read.

  colour   sRGB, the ambient occlusion in alpha
  surface  red, green: the normal's x, y in tangent space (128 flat); blue: opacity (the forearm
           fades out past the elbow); alpha: the material band (pit_hand_shader.h)"""
import numpy as np

import arm_build as B
import arm_gear as G
import arm_shape as A
import arm_skel as S
from arm_shape import smoothstep

MATERIAL_ALPHA = {G.GOLD: 0.10, G.ENAMEL: 0.30, G.LEATHER: 0.50, G.SKIN: 0.70, G.CLOTH: 0.90, G.DARK: 0.90}


def rgb(h):
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], float) / 255.0


SKIN = rgb('F3DDBA')          # in the light the shader lifts it to the pictures' pale peach
SKIN_ROSY = rgb('F0B79E')     # tips, knuckles, the heel of the hand
SKIN_CREASE = rgb('D9916F')   # warm folds
GLOVE = rgb('4C6072')
GLOVE_DARK = rgb('36475A')
GOLD = rgb('DDBE56')
GOLD_LIGHT = rgb('F0DC7C')
GOLD_DEEP = rgb('B8913C')
LEATHER = rgb('8A5632')
LEATHER_DARK = rgb('5A3620')
LEATHER_LIGHT = rgb('A06A40')
RED = rgb('84302C')
RED_DEEP = rgb('611E22')
INSIDE = rgb('1B1D24')


# ---- noise: a lattice of hashed values, no random generator ----
def hash3(i, j, k, seed):
    h = (i.astype(np.uint64) * np.uint64(73856093)) ^ (j.astype(np.uint64) * np.uint64(19349663)) ^ \
        (k.astype(np.uint64) * np.uint64(83492791)) ^ np.uint64(seed * 2654435761 % (1 << 32))
    h = (h ^ (h >> np.uint64(13))) * np.uint64(1274126177) % np.uint64(1 << 32)
    h = h ^ (h >> np.uint64(16))
    return (h % np.uint64(65536)).astype(float) / 65535.0


def value_noise(p, scale, seed):
    q = p / scale + 1000.0
    i = np.floor(q).astype(np.int64)
    f = q - i
    f = f * f * (3 - 2 * f)
    out = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                out = out + w * hash3(i[:, 0] + dx, i[:, 1] + dy, i[:, 2] + dz, seed)
    return out


def fbm(p, scale, seed, octaves=3):
    out, amp, total = 0.0, 1.0, 0.0
    for o in range(octaves):
        out = out + amp * value_noise(p, scale / (2 ** o), seed + o)
        total += amp
        amp *= 0.5
    return out / total


# ---- the atlas: which point of the arm each texel covers ----
def rasterise(mesh, size):
    uv = mesh['uv'] * size
    tris = mesh['tris']
    tri_id = np.full((size, size), -1, np.int32)
    bary = np.zeros((size, size, 3))
    for i, t in enumerate(tris):
        (x0, y0), (x1, y1), (x2, y2) = uv[t[0]], uv[t[1]], uv[t[2]]
        a = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(a) < 1e-12:
            continue
        ax, bx = max(int(np.floor(min(x0, x1, x2) - 1)), 0), min(int(np.ceil(max(x0, x1, x2) + 1)), size - 1)
        ay, by = max(int(np.floor(min(y0, y1, y2) - 1)), 0), min(int(np.ceil(max(y0, y1, y2) + 1)), size - 1)
        px = np.arange(ax, bx + 1) + 0.5
        py = (np.arange(ay, by + 1) + 0.5)[:, None]
        w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / a
        w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / a
        w2 = 1.0 - w0 - w1
        m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not m.any():
            continue
        tri_id[ay:by + 1, ax:bx + 1][m] = i
        bary[ay:by + 1, ax:bx + 1][m] = np.stack([w0, w1, w2], -1)[m]
    return tri_id, bary


def dilate(img, known, rounds):
    """Spreads known texels into unknown neighbours, so filtering never meets an empty texel."""
    img = img.astype(float)
    known = known.copy()
    for _ in range(rounds):
        acc = np.zeros_like(img)
        cnt = np.zeros(known.shape)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                k = np.roll(np.roll(known, dy, 0), dx, 1)
                acc += np.roll(np.roll(img, dy, 0), dx, 1) * k[..., None]
                cnt += k
        new = (~known) & (cnt > 0)
        img[new] = acc[new] / cnt[new][:, None]
        known |= new
    return img, known


# ---- the hand: where things are on it ----
def digit_marks(p):
    """Rosy tips and knuckles, fold lines at the joints, a soft nail shape: (rosy 0..1, crease
    0..1, nail height 0..1) for points on the hand."""
    rosy = np.zeros(len(p))
    crease = np.zeros(len(p))
    nail = np.zeros(len(p))
    P, R = S.BIND_P, S.BIND_R
    chains = [(list(S.THUMB[1:]), S.THUMB_R[1:], True)] + [(list(c[1:]), S.FINGER_R[c], False) for c in S.FINGERS]
    for joints, radii, thumb in chains:
        tip = P[joints[-1]]
        last = P[joints[-2]]
        d = S.norm(tip - last)
        up = R[joints[-2]][:, 1]
        r_tip = radii[-1]
        # the tip: rosy, more underneath
        q = p - (tip - d * r_tip)
        dist = np.linalg.norm(q, axis=1)
        under = 0.75 - 0.25 * np.clip((q @ up) / r_tip, -1, 1)
        rosy = np.maximum(rosy, (1 - smoothstep(r_tip * 0.9, r_tip * 2.3, dist)) * under)
        # a nail: hardly there: a low, soft shape on the back of the last bone
        c = last + (tip - last) * 0.60 + up * radii[-2] * 0.9
        qn = p - c
        along, across = qn @ d, qn @ R[joints[-2]][:, 0]
        e = (along / (r_tip * 0.80)) ** 2 + (across / (r_tip * 0.66)) ** 2
        nail = np.maximum(nail, (1 - smoothstep(0.55, 1.0, e)) * ((qn @ up) > -r_tip * 0.6) * (1.0 if thumb else 0.6))
        # the joints: a rosy knuckle on the back, a fold line underneath (and a fainter one on top)
        for k, j in enumerate(joints[:-1]):
            jp = P[j]
            if k == 0 and not thumb:
                dj = S.norm(P[joints[1]] - jp)
            else:
                dj = S.norm(S.norm(P[joints[k + 1]] - jp) + (S.norm(jp - P[joints[k - 1]]) if k else 0))
            uj = R[j][:, 1]
            q = p - jp
            near = np.linalg.norm(q, axis=1) < radii[k] * 1.9
            h = np.clip((q @ uj) / radii[k], -1, 1)
            knuckle = np.exp(-((np.linalg.norm(q - uj * radii[k] * 0.9, axis=1) / (radii[k] * 0.95)) ** 2))
            rosy = np.maximum(rosy, knuckle * (0.75 if k == 0 else 0.55))
            if k > 0 or thumb:
                line = np.exp(-(((q @ dj) / 0.00065) ** 2)) * near
                crease = np.maximum(crease, line * np.where(h < 0.1, 1.0, 0.30))
                side = np.exp(-(((q @ dj) + 0.0022) / 0.0005) ** 2) * near * (h > 0.45) * 0.22
                crease = np.maximum(crease, side)
    # the folds where the fingers meet the palm
    for c in S.FINGERS:
        k, m = P[c[1]], P[c[2]]
        d = S.norm(m - k)
        q = p - (k + (m - k) * (A.WEB_T - 0.02))
        near = (np.linalg.norm(q, axis=1) < S.FINGER_R[c][0] * 1.5) & ((q @ R[c[1]][:, 1]) < -S.FINGER_R[c][0] * 0.35)
        crease = np.maximum(crease, np.exp(-(((q @ d) / 0.0007) ** 2)) * near * 0.8)
    # the heel of the hand and the thumb's ball: a little rosy
    rosy = np.maximum(rosy, 0.35 * np.exp(-((np.linalg.norm(p - np.array([0.030, -0.022, 0.030]), axis=1) / 0.020) ** 2)))
    return rosy, crease, nail


def hand_height(p):
    """The hand's fine relief in metres: the glove's raised edge and hem, the folds, the nails."""
    g = A.glove_sd(p)
    _, crease, nail = digit_marks(p)
    cloth = g < 0
    h = 0.00045 * smoothstep(0.00035, -0.00035, g)
    h += 0.00018 * np.exp(-(((g + 0.0016) / 0.0005) ** 2))          # the hem's stitch line
    h -= np.where(cloth, 0.0, 0.00035 * crease)
    h += np.where(cloth, 0.0, 0.00022 * nail)
    h += np.where(cloth, 0.00004 * (value_noise(p, 0.0007, 5) - 0.5), 0.0)
    return h


def hand_colour(p):
    g = A.glove_sd(p)
    rosy, crease, nail = digit_marks(p)
    skin = SKIN + (SKIN_ROSY - SKIN) * (rosy * 0.85)[:, None]
    skin = skin + (SKIN_CREASE - skin) * (crease * 0.75)[:, None]
    skin = skin + (np.array([1.0, 0.93, 0.88]) - skin) * (nail * 0.10)[:, None]
    skin = skin * (0.985 + 0.03 * fbm(p, 0.012, 11))[:, None]
    weave = fbm(p, 0.0012, 3, 2)
    cloth = GLOVE + (GLOVE_DARK - GLOVE) * (0.35 * weave + 0.5 * np.exp(-(((g + 0.0016) / 0.0006) ** 2)))[:, None]
    cloth = cloth * (1.0 - 0.25 * smoothstep(-0.0008, 0.0, g))[:, None]
    is_cloth = g < 0
    # a thin warm shadow on the skin along the glove's edge
    skin = skin + (SKIN_CREASE - skin) * (0.55 * (1 - smoothstep(0.0, 0.0012, g)))[:, None]
    return np.where(is_cloth[:, None], cloth, skin), is_cloth


# ---- the leather: grain and the tooled scroll beside each band ----
SCROLL_W = 0.0105      # the scroll band's width
SCROLL_GAP = 0.0030    # from a gold band's edge to the scroll band
SCROLL_N = 18          # scrolls round the arm


def scroll_motif():
    """One scroll, as points in a unit cell (along 0..1, across 0..1)."""
    pts = []
    u = np.linspace(0, 1, 40)
    pts.append(np.stack([u, 0.5 + 0.20 * np.sin(2 * np.pi * u)], 1))
    for c, turn, start in (((0.27, 0.42), 1.0, np.pi * 0.5), ((0.77, 0.58), 1.0, -np.pi * 0.5)):
        th = np.linspace(0, 2.6 * np.pi, 46)
        r = 0.27 * (1 - th / (3.1 * np.pi))
        pts.append(np.stack([c[0] + r * np.cos(start + turn * th) * 0.80, c[1] + r * np.sin(start + turn * th)], 1))
    for c in ((0.02, 0.22), (0.52, 0.80)):   # a bud between scrolls
        th = np.linspace(0, 2 * np.pi, 12)
        pts.append(np.stack([c[0] + 0.035 * np.cos(th), c[1] + 0.06 * np.sin(th)], 1))
    return np.concatenate(pts)


def leather_tooling(p):
    """How deep the tooling is at points on the leather (0 none to 1), and the grain."""
    x = p[:, 0]
    ang = np.arctan2(p[:, 2], p[:, 1])
    r_mean = 0.045
    out = np.zeros(len(p))
    motif = scroll_motif()
    for edge, sign in ((G.BAND_WRIST_X - G.BAND_HALF, -1.0), (G.BAND_ELBOW_X + G.BAND_HALF, 1.0)):
        across = (x - edge) * sign - SCROLL_GAP      # 0 at the scroll band's near border
        m = (across > -0.0015) & (across < SCROLL_W + 0.0015)
        if not m.any():
            continue
        a = across[m]
        cell = 2 * np.pi / SCROLL_N
        u = (ang[m] % cell) / cell
        if sign > 0:
            u = 1 - u
        q = np.stack([u, a / SCROLL_W], 1)
        d = np.full(len(q), 9.0)
        scale = np.array([cell * r_mean, SCROLL_W])
        for shift in (-1.0, 0.0, 1.0):
            mm = (motif + np.array([shift, 0.0])) * scale
            for s in range(0, len(q), 20000):
                qq = q[s:s + 20000] * scale
                d[s:s + 20000] = np.minimum(d[s:s + 20000], np.sqrt(((qq[:, None, :] - mm[None]) ** 2).sum(2)).min(1))
        line = 1 - smoothstep(0.00025, 0.00060, d)
        border = np.maximum(np.exp(-((a / 0.00040) ** 2)), np.exp(-(((a - SCROLL_W) / 0.00040) ** 2)))
        inside = (a > 0) & (a < SCROLL_W)
        out[m] = np.maximum(line * inside, border)
    return out


def leather_height(p):
    return -0.00030 * leather_tooling(p) + 0.00007 * (fbm(p, 0.0016, 21) - 0.5)


def leather_colour(p):
    tool = leather_tooling(p)
    grain = fbm(p, 0.0022, 23)
    blotch = fbm(p, 0.022, 29, 2)
    c = LEATHER + (LEATHER_LIGHT - LEATHER) * ((blotch - 0.5) * 1.2)[:, None]
    c = c * (0.90 + 0.18 * grain)[:, None]
    return c + (LEATHER_DARK - c) * (tool * 0.80)[:, None]


def gold_colour(p, n):
    streak = fbm(p * np.array([0.25, 1.0, 1.0]), 0.004, 31, 2)
    c = GOLD + (GOLD_DEEP - GOLD) * ((streak - 0.5) * 0.5)[:, None]
    return np.clip(c, 0, 1)


def occlusion(p, n, with_plate=False):
    """Short reach ambient occlusion from the fields the shapes were made from."""
    occ = np.zeros(len(p))
    for h, w in ((0.0025, 0.30), (0.006, 0.38), (0.012, 0.32)):
        q = p + n * h
        d = np.minimum(A.hand_field(q), G.gear_sd(q))
        if with_plate:
            d = np.minimum(d, G.plate_sd(q))
        occ += w * np.clip((h - d) / h, 0, 1)
    return 1.0 - occ


def paint(mesh, size=B.TEX, log=print):
    """Returns the colour and surface images, (size, size, 4) uint8."""
    tri_id, bary = rasterise(mesh, size)
    known = tri_id >= 0
    ys, xs = np.nonzero(known)
    t = mesh['tris'][tri_id[known]]
    b = bary[known]

    def at(attr):
        return attr[t[:, 0]] * b[:, :1] + attr[t[:, 1]] * b[:, 1:2] + attr[t[:, 2]] * b[:, 2:3]

    p = at(mesh['v'])
    n = S.norm(at(mesh['n']))
    tan = at(mesh['tan'][:, :3])
    tan = S.norm(tan - n * (tan * n).sum(1, keepdims=True))
    sign = np.where(at(mesh['tan'][:, 3:4])[:, 0] < 0, -1.0, 1.0)
    bit = np.cross(n, tan) * sign[:, None]
    part = mesh['tri_part'][tri_id[known]]
    mat = mesh['tri_mat'][tri_id[known]].copy()
    count = len(p)
    col = np.zeros((count, 3))
    ao = np.ones(count)
    slope = np.zeros((count, 2))
    opacity = np.ones(count)
    eps = 0.00030

    def relief(m, height):
        h0 = height(p[m])
        slope[m, 0] = -(height(p[m] + tan[m] * eps) - h0) / eps
        slope[m, 1] = -(height(p[m] + bit[m] * eps) - h0) / eps

    m = part == B.HAND
    log(f'  hand texels: {m.sum()}')
    c, cloth = hand_colour(p[m])
    col[m] = c
    mm = mat[m]
    mm[cloth] = G.CLOTH
    mat[m] = mm
    relief(m, hand_height)
    ao[m] = occlusion(p[m], n[m])
    # inside the wrist band it is dark
    ao[m] *= 0.35 + 0.65 * smoothstep(G.BAND_WRIST_X + G.BAND_HALF - 0.012, G.BAND_WRIST_X + G.BAND_HALF + 0.006, p[m][:, 0])

    m = part == B.LEATHER
    col[m] = leather_colour(p[m])
    relief(m, leather_height)
    x = p[m][:, 0]
    to_band = np.minimum(np.abs(x - (G.BAND_WRIST_X - G.BAND_HALF)), np.abs(x - (G.BAND_ELBOW_X + G.BAND_HALF)))
    ao[m] = 0.45 + 0.55 * smoothstep(0.0, 0.0065, to_band)

    for pid in (B.BAND_W, B.BAND_E):
        m = part == pid
        col[m] = gold_colour(p[m], n[m])
        r = np.hypot(p[m][:, 1], p[m][:, 2])
        inner = r < G.BAND_RI + 0.0003
        edge = np.exp(-(((r - (G.BAND_RO - G.BAND_BEVEL * 0.5)) / 0.0016) ** 2)) * (r > G.BAND_RO - G.BAND_BEVEL - 0.0005)
        col[m] = col[m] + (GOLD_LIGHT - col[m]) * (0.45 * edge)[:, None]
        ao[m] = np.where(inner, 0.42, 0.72 + 0.28 * smoothstep(G.BAND_RI, G.BAND_RI + 0.006, r))

    for pid in (B.FLOOR_W, B.FLOOR_E):
        m = part == pid
        col[m] = INSIDE
        ao[m] = 0.30

    m = part == B.STUB
    col[m] = SKIN * (0.985 + 0.03 * fbm(p[m], 0.012, 11))[:, None]
    ao[m] = 0.45 + 0.55 * smoothstep(G.BAND_ELBOW_X - G.BAND_HALF + 0.002, G.BAND_ELBOW_X - G.BAND_HALF - 0.010, p[m][:, 0])
    opacity[m] = smoothstep(G.STUB_FADE[1], G.STUB_FADE[0], p[m][:, 0])

    m = part == B.PLATE_TOP
    gold = mat[m] == G.GOLD
    c = np.where(gold[:, None], gold_colour(p[m], n[m]), RED + (RED_DEEP - RED) * (fbm(p[m], 0.006, 41, 2) - 0.3)[:, None] * 0.5)
    col[m] = c
    ao[m] = np.where(gold, 1.0, 0.86)
    m = part == B.PLATE_UNDER
    col[m] = GOLD_DEEP
    ao[m] = np.where(n[m][:, 1] < -0.5, 0.45, 0.85)

    colour = np.zeros((size, size, 4))
    surface = np.zeros((size, size, 4))
    colour[ys, xs, :3] = np.clip(col, 0, 1)
    colour[ys, xs, 3] = np.clip(ao, 0, 1)
    slope = np.clip(slope * 1.0, -0.75, 0.75)
    nx = slope / np.sqrt(1 + (slope ** 2).sum(1, keepdims=True))
    surface[ys, xs, 0:2] = 128.0 / 255.0 + nx * (127.0 / 255.0)
    surface[ys, xs, 2] = opacity
    surface[ys, xs, 3] = np.array([MATERIAL_ALPHA[int(k)] for k in range(6)])[mat]
    colour, _ = dilate(colour, known, 8)
    surface, known2 = dilate(surface, known, 8)
    # what no triangle is near: flat, solid, skin (never sampled)
    surface[~known2] = (128 / 255.0, 128 / 255.0, 1.0, 0.70)
    colour[~known2] = (*SKIN, 1.0)
    to8 = lambda a: np.clip(np.floor(a * 255 + 0.5), 0, 255).astype(np.uint8)
    return to8(colour), to8(surface)
