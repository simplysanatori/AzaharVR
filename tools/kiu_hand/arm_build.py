# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Pit's arm, drawn: the whole model put together: the hand's skin (arm_shape.py) and what the
forearm wears (arm_gear.py) as one vertex list, with one texture atlas, tangents and skin
weights. The triangles are in two ranges: the body, which both arms draw, then the plate, which
only the left arm draws. Nothing in the body or its textures is one-sided, so the right arm is
the body mirrored."""
import numpy as np

import arm_gear as G
import arm_shape as A
import arm_skel as S
from arm_skel import norm

TEX = 1024
GUTTER = 6
# part ids
HAND, BAND_W, BAND_E, LEATHER, FLOOR_W, FLOOR_E, STUB, PLATE_TOP, PLATE_UNDER = range(9)
PART_NAMES = ['hand', 'band_wrist', 'band_elbow', 'leather', 'floor_wrist', 'floor_elbow', 'stub', 'plate_top',
              'plate_under']
# the atlas: chart -> (x, y, width, height) in texels of a TEX square
RECTS = {
    'tube': (0, 0, 560, 400), 'digit1': (560, 0, 190, 300), 'plate_top': (750, 0, 150, 250),
    'stub': (900, 0, 124, 200), 'floor_wrist': (900, 200, 62, 62), 'floor_elbow': (962, 200, 62, 62),
    'plate_under': (750, 250, 60, 50), 'side': (812, 264, 212, 136),
    'digit2': (0, 400, 256, 240), 'digit3': (256, 400, 256, 240), 'digit4': (512, 400, 256, 240),
    'digit5': (768, 400, 256, 240),
    'leather': (0, 640, 1024, 200), 'band_wrist': (0, 840, 1024, 92), 'band_elbow': (0, 932, 1024, 92),
}
TUBE_R = 0.034
SIDE_CHART = 6


def digit_chain(tag):
    if tag == 1:
        return [S.BIND_P[j] for j in S.THUMB], [S.BIND_R[j] for j in S.THUMB[:3]], S.THUMB_R[1]
    ch = S.FINGERS[tag - 2]
    return [S.BIND_P[j] for j in ch[1:]], [S.BIND_R[j] for j in ch[1:4]], S.FINGER_R[ch][1]


def hand_chart_coords(p, tag):
    """(turn round, length along) of points on chart `tag` (0 the tube, 1..5 a digit): the turn
    in radians (0 on the back, +-pi under), the length in metres."""
    if tag == SIDE_CHART:
        return p[:, 1] / TUBE_R, p[:, 0], np.ones(len(p))
    if tag == 0:
        return np.arctan2(p[:, 2], (p[:, 1] + 0.002) * 2.0), p[:, 0], np.ones(len(p))
    joints, frames, _ = digit_chain(tag)
    best = np.full(len(p), np.inf)
    turn = np.zeros(len(p))
    along = np.zeros(len(p))
    at = 0.0
    for i in range(3):
        a, b = joints[i], joints[i + 1]
        ln = np.linalg.norm(b - a)
        d = (b - a) / ln
        s = (p - a) @ d
        sc = np.clip(s, 0 if i else -1.0, ln if i < 2 else 1.0)
        q = p - a - sc[:, None] * d
        dist = np.linalg.norm(q, axis=1)
        take = dist < best
        best = np.where(take, dist, best)
        # one frame for the whole digit, so the turn does not jump between bones
        qq = p - a - s[:, None] * d
        th = np.arctan2(qq @ frames[1][:, 0], qq @ frames[1][:, 1])
        turn = np.where(take, th, turn)
        along = np.where(take, at + s, along)
        at += ln
    return turn, along, np.minimum(best / digit_chain(tag)[2], 1.0)


def unwrap_hand(v, tris, ttag):
    """Per corner chart coordinates (T, 3, 2) in metres, the seam under the hand."""
    uv = np.zeros((len(tris), 3, 2))
    for tag in range(7):
        m = ttag == tag
        if not m.any():
            continue
        t = tris[m]
        turn, along, narrow = hand_chart_coords(v[t.reshape(-1)], tag)
        turn = turn.reshape(-1, 3)
        wrap = (turn.max(1) - turn.min(1)) > np.pi
        turn = np.where(wrap[:, None] & (turn < 0), turn + 2 * np.pi, turn)
        r = TUBE_R if tag in (0, SIDE_CHART) else digit_chain(tag)[2]
        uv[m] = np.stack([turn * r * narrow.reshape(-1, 3), along.reshape(-1, 3)], 2)   # a tip closes to a point
    return uv


def to_rect(uv, name, keep_aspect=False):
    x, y, w, h = RECTS[name]
    lo, hi = uv.min(0), uv.max(0)
    span = np.maximum(hi - lo, 1e-9)
    out = (uv - lo) / span
    return np.stack([(x + GUTTER + out[:, 0] * (w - 2 * GUTTER)) / TEX, (y + GUTTER + out[:, 1] * (h - 2 * GUTTER)) / TEX], 1)


def tangents(v, n, uv, tris):
    p0, p1, p2 = v[tris[:, 0]], v[tris[:, 1]], v[tris[:, 2]]
    d1, d2 = uv[tris[:, 1]] - uv[tris[:, 0]], uv[tris[:, 2]] - uv[tris[:, 0]]
    det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    det = np.where(np.abs(det) < 1e-14, 1e-14, det)
    tu = ((p1 - p0) * d2[:, 1:2] - (p2 - p0) * d1[:, 1:2]) / det[:, None]
    tv = ((p2 - p0) * d1[:, 0:1] - (p1 - p0) * d2[:, 0:1]) / det[:, None]
    area = np.linalg.norm(np.cross(p1 - p0, p2 - p0), axis=1, keepdims=True)
    tu, tv = norm(tu) * area, norm(tv) * area
    t = np.zeros_like(v)
    b = np.zeros_like(v)
    for k in range(3):
        np.add.at(t, tris[:, k], tu)
        np.add.at(b, tris[:, k], tv)
    t = t - n * (t * n).sum(1, keepdims=True)
    bad = np.linalg.norm(t, axis=1) < 1e-12
    t[bad] = np.cross(n[bad], [0.0, 0.0, 1.0]) + 1e-9
    t = norm(t)
    sign = np.where((np.cross(n, t) * b).sum(1) < 0, -1.0, 1.0)
    return np.concatenate([t, sign[:, None]], 1)


def rigid(count, pairs):
    bones = np.zeros((count, 4), int)
    weights = np.zeros((count, 4))
    for k, (j, w) in enumerate(pairs):
        bones[:, k] = j
        weights[:, k] = w
    bones[:, len(pairs):] = pairs[0][0]
    return bones, weights


PLATE_BONES = [(S.WRIST, 0.6), (S.FOREARM, 0.4)]   # goes with the hand, held by the band


def assemble(log=print):
    hv, htris, httag, hvt, hn = A.build_hand(log=log)
    # the hand: weld per corner chart coordinates into vertices
    # the tube's faces that look toward the thumb's side (its root, the web beside it) fold
    # over in the tube's chart, which turns round the arm: they get a flat chart of their own
    fn = norm(np.cross(hv[htris[:, 1]] - hv[htris[:, 0]], hv[htris[:, 2]] - hv[htris[:, 0]]))
    mid = hv[htris].mean(1)
    httag = np.where((httag == 0) & (fn[:, 2] > 0.50) & (mid[:, 0] > 0.004), SIDE_CHART, httag)
    cuv = unwrap_hand(hv, htris, httag)
    names = ['tube'] + [f'digit{i}' for i in range(1, 6)] + ['side']
    key = np.stack([htris.reshape(-1), np.repeat(httag, 3), np.round(cuv[:, :, 0].reshape(-1) * 2e6),
                    np.round(cuv[:, :, 1].reshape(-1) * 2e6)], 1).astype(np.int64)
    uniq, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    src = uniq[:, 0]
    chart = uniq[:, 1]
    raw = cuv.reshape(-1, 2)[first]
    uv = np.zeros((len(uniq), 2))
    for tag in range(7):
        m = chart == tag
        uv[m] = to_rect(raw[m], names[tag])
    w = A.hand_weights(hv, hvt)
    hb, hw = A.prune(w)
    parts = [dict(v=hv[src], n=hn[src], tris=inv.reshape(-1, 3), uv=uv, bones=hb[src], weights=hw[src],
                  mat=np.full(len(htris), G.SKIN), part=HAND, digit=httag)]

    def add(part, name, v, n, tris, uv, mat, pairs):
        b, wt = rigid(len(v), pairs)
        mats = np.full(len(tris), mat) if np.isscalar(mat) else mat
        parts.append(dict(v=v, n=n, tris=tris, uv=to_rect(uv, name), bones=b, weights=wt, mat=mats, part=part,
                          digit=np.zeros(len(tris), int)))

    fore = [(S.FOREARM, 1.0)]
    add(LEATHER, 'leather', *G.leather(), G.LEATHER, fore)
    add(BAND_W, 'band_wrist', *G.band(G.BAND_WRIST_X), G.GOLD, fore)
    add(BAND_E, 'band_elbow', *G.band(G.BAND_ELBOW_X), G.GOLD, fore)
    add(FLOOR_W, 'floor_wrist', *G.floor(G.BAND_WRIST_X + G.BAND_HALF - G.FLOOR_IN, 1), G.DARK, fore)
    add(FLOOR_E, 'floor_elbow', *G.floor(G.BAND_ELBOW_X - G.BAND_HALF + G.FLOOR_IN, -1), G.DARK, fore)
    add(STUB, 'stub', *G.stub(), G.SKIN, fore)
    top, under = G.plate()
    add(PLATE_TOP, 'plate_top', top[0], top[1], top[2], top[3], top[4], PLATE_BONES)
    add(PLATE_UNDER, 'plate_under', *under, G.GOLD, PLATE_BONES)
    out = {}
    off = 0
    tris, tri_mat, tri_part, tri_digit = [], [], [], []
    for p in parts:
        tris.append(p['tris'] + off)
        tri_mat.append(p['mat'])
        tri_part.append(np.full(len(p['tris']), p['part']))
        tri_digit.append(p['digit'])
        off += len(p['v'])
    for k in ('v', 'n', 'uv', 'bones', 'weights'):
        out[k] = np.concatenate([p[k] for p in parts])
    out['tris'] = np.concatenate(tris)
    out['tri_mat'] = np.concatenate(tri_mat)
    out['tri_part'] = np.concatenate(tri_part)
    out['tri_digit'] = np.concatenate(tri_digit)
    out['plate_tris'] = int((out['tri_part'] >= PLATE_TOP).sum())
    assert (out['tri_part'][-out['plate_tris']:] >= PLATE_TOP).all()
    out['tan'] = tangents(out['v'], out['n'], out['uv'], out['tris'])
    log(f"arm: {len(out['v'])} vertices, {len(out['tris'])} triangles ({out['plate_tris']} the plate's)")
    for i, name in enumerate(PART_NAMES):
        log(f'  {name}: {(out["tri_part"] == i).sum()} triangles')
    return out


def posed(mesh, p, r):
    """The mesh in a pose (arm_skel.to_arm's joints): a copy with v, n, tan moved."""
    mats = S.bone_matrices(p, r)
    v, n = S.skin(mesh['v'], mesh['n'], mesh['bones'], mesh['weights'], mats)
    _, t = S.skin(mesh['v'], mesh['tan'][:, :3], mesh['bones'], mesh['weights'], mats)
    out = dict(mesh)
    out.update(v=v, n=n, tan=np.concatenate([t, mesh['tan'][:, 3:]], 1))
    return out
