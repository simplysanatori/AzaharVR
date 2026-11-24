#!/usr/bin/env python3
# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Writes Pit's hand and the laurel frame as C++ data:

    python3 tools/kiu_hand/gen.py            (from the repository's root; numpy only)

makes src/frame_xr/vis/pit_hand_data.inc and src/frame_xr/vis/laurel_frame_data.inc from the
numbers in hand.py, mesh.py, build.py, poses.py and laurel.py alone: it reads no other file, so
what it writes is ours. The same numbers give the same bytes. `--cache DIR` keeps the built mesh
and the solved poses between runs while a shape is being worked on."""
import argparse
import hashlib
import os
import pickle
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import build as B   # noqa: E402
import hand as H    # noqa: E402
import laurel as L  # noqa: E402
import mesh as M    # noqa: E402
import poses as PZ  # noqa: E402

THUMB_KEYS = ['open', 'relaxed', 'fist', 'up', 'held_up', 'held_rest', 'held_stick', 'held_buttons']
INDEX_KEYS = ['open', 'relaxed', 'fist', 'point', 'held_off', 'held_touch', 'held_pull']
REST_KEYS = ['open', 'relaxed', 'fist', 'held_open', 'held']
THUMB_JOINTS = list(H.THUMB[:3])
INDEX_JOINTS = list(H.INDEX[:4])
REST_JOINTS = list(H.MIDDLE[:4]) + list(H.RING[:4]) + list(H.LITTLE[:4])


def cached(cache, name, sources, make):
    if not cache:
        return make()
    key = hashlib.sha256(''.join(open(os.path.join(HERE, s)).read() for s in sources).encode()).hexdigest()[:16]
    path = os.path.join(cache, f'{name}-{key}.pkl')
    if os.path.exists(path):
        return pickle.load(open(path, 'rb'))
    out = make()
    os.makedirs(cache, exist_ok=True)
    pickle.dump(out, open(path, 'wb'))
    return out


def hand_mesh(cache=None, log=print):
    return cached(cache, 'mesh', ['hand.py', 'mesh.py', 'build.py'], lambda: B.merged(B.build(log=log)))


def pose_keys(cache=None, log=print):
    return cached(cache, 'keys', ['hand.py', 'mesh.py', 'poses.py'], lambda: PZ.keys(log))


def laurel_mesh():
    """The wreath and its plate with hard edges split: v, n, tris, material, shade, ranges."""
    vs, ns, ts, ms, ss, ranges = [], [], [], [], [], []
    off = 0
    tri_at = 0
    for name, mat, v, t, shade in L.build():
        v2, n2, t2, src = M.split_creases(v, t, 35.0 if name == 'laurel' else 40.0)
        vs.append(v2)
        ns.append(n2)
        ts.append(t2 + off)
        ms.append(np.full(len(v2), mat, int))
        ss.append(shade[src])
        if ranges and ranges[-1][0] == name:
            ranges[-1] = (name, ranges[-1][1], ranges[-1][2] + len(t2))
        else:
            ranges.append((name, tri_at, len(t2)))
        tri_at += len(t2)
        off += len(v2)
    return (np.concatenate(vs), np.concatenate(ns), np.concatenate(ts), np.concatenate(ms),
            np.concatenate(ss), ranges)


# ---- writing ----


def fmt_f(x):
    s = f'{float(x):.6g}'
    if 'e' not in s and '.' not in s:
        s += '.0'
    return s + 'f'


def array(out, ctype, name, values, per_line=24, fmt=str):
    vals = [fmt(v) for v in values]
    out.append(f'constexpr {ctype} {name}[{len(vals)}] = {{')
    for i in range(0, len(vals), per_line):
        out.append(','.join(vals[i:i + per_line]) + ',')
    out.append('};')


def quantise(v):
    lo, hi = v.min(0), v.max(0)
    scale = (hi - lo) / 65535.0
    q = np.clip(np.floor((v - lo) / scale + 0.5), 0, 65535).astype(int)
    return lo, scale, q


def write_mesh(out, prefix, v, n, tris, material, shade, tint=None):
    lo, scale, q = quantise(v)
    out.append(f'constexpr uint32_t k{prefix}VertexCount = {len(v)};')
    out.append(f'constexpr uint32_t k{prefix}TriangleCount = {len(tris)};')
    array(out, 'float', f'k{prefix}PosMin', lo, fmt=fmt_f)
    array(out, 'float', f'k{prefix}PosScale', scale, fmt=fmt_f)
    array(out, 'uint16_t', f'k{prefix}Pos', q.reshape(-1))
    array(out, 'int8_t', f'k{prefix}Normal', np.clip(np.floor(n * 127 + 0.5), -127, 127).astype(int).reshape(-1), 32)
    array(out, 'uint8_t', f'k{prefix}Material', material, 48)
    array(out, 'uint8_t', f'k{prefix}Shade', np.clip(np.floor(shade * 255 + 0.5), 0, 255).astype(int), 32)
    if tint is not None:
        array(out, 'uint8_t', f'k{prefix}Tint', np.clip(np.floor(tint * 255 + 0.5), 0, 255).astype(int), 32)
    array(out, 'uint16_t', f'k{prefix}Index', tris.reshape(-1))


HEADER = '''// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Made by tools/kiu_hand/gen.py from its own numbers (no game data). Do not edit: change the
// generator and run it again.
'''


def quats(local, joints):
    return [H.quat_from_mat(local[j]) for j in joints]


def write_hand(path, m, ranges, k, log=print):
    thumb, index, rest, named, held_cyl, (cr, ct) = k
    out = [HEADER]
    v = m['v'] / H.UNIT
    assert len(v) < 65536
    write_mesh(out, 'Pit', v, m['n'], m['tris'], m['material'], m['shade'], m['tint'])
    array(out, 'uint8_t', 'kPitBone', m['bones'].reshape(-1), 48)
    array(out, 'uint8_t', 'kPitWeight', m['weights'].reshape(-1), 32)
    out.append('struct PitPartData { const char* name; uint8_t material; uint32_t first_index; uint32_t index_count; };')
    out.append(f'constexpr PitPartData kPitParts[{len(ranges)}] = {{')
    for name, first, count in ranges:
        mat = int(m['material'][m['tris'][first][0]])
        out.append(f'    {{"{name}", {mat}, {first * 3}, {count * 3}}},')
    out.append('};')
    # the skeleton
    array(out, 'int8_t', 'kPitParent', H.PARENT, 32)
    array(out, 'float', 'kPitLocalT', (H.LOCAL_T / H.UNIT).reshape(-1), 6, fmt_f)
    array(out, 'float', 'kPitBindLocal', np.array(quats(H.BIND_LOCAL, range(H.NJ))).reshape(-1), 8, fmt_f)
    array(out, 'float', 'kPitFlatLocal', np.array(quats(H.FLAT_LOCAL, range(H.NJ))).reshape(-1), 8, fmt_f)
    radius = np.zeros(H.NJ)
    for c in H.FINGERS:
        radius[c[0]] = PZ.PALM_R
        for j, r in zip(c[1:], H.FINGER_R[c]):
            radius[j] = r
    for j, r in zip(H.THUMB, H.THUMB_R):
        radius[j] = r
    array(out, 'float', 'kPitRadius', radius / H.UNIT, 8, fmt_f)
    # the key poses
    def key_table(name, keys, joints, angles_of):
        rows = []
        for key in keys:
            fingers, th, cup = angles_of(key)
            rows += quats(H.pose_local(fingers, th, cup), joints)
        out.append(f'constexpr int k{name}KeyCount = {len(keys)};  // {", ".join(keys)}')
        array(out, 'float', f'k{name}Keys', np.array(rows).reshape(-1), 8, fmt_f)

    zero = [(0, 0, 0, 0)] * 4
    key_table('PitThumb', THUMB_KEYS, THUMB_JOINTS, lambda key: (zero, thumb[key], 0))
    key_table('PitIndex', INDEX_KEYS, INDEX_JOINTS, lambda key: ([index[key]] + zero[1:], H.BIND_THUMB, 0))
    key_table('PitRest', REST_KEYS, REST_JOINTS, lambda key: ([zero[0]] + list(rest[key][0]), H.BIND_THUMB, rest[key][1]))
    order = ['open', 'relaxed', 'fist', 'point', 'thumb_up', 'grip', 'trigger', 'grip_thumb_up', 'grip_stick',
             'grip_buttons', 'grip_open']
    out.append(f'constexpr uint8_t kPitNamed[{len(order)}][3] = {{  // thumb, index, rest keys')
    for name in order:
        t, i, r = named[name]
        out.append(f'    {{{THUMB_KEYS.index(t)}, {INDEX_KEYS.index(i)}, {REST_KEYS.index(r)}}},  // {name}')
    out.append('};')
    # handles and fits
    c, ax, rad = held_cyl
    array(out, 'float', 'kPitHeld', list(c / H.UNIT) + list(ax) + [rad / H.UNIT], 7, fmt_f)
    fc, fax = PZ.hand_cylinder(PZ.FIST_RADIUS, PZ.FIST_X, PZ.FIST_TILT_DEG)
    array(out, 'float', 'kPitFist', list(fc / H.UNIT) + list(fax) + [PZ.FIST_RADIUS / H.UNIT], 7, fmt_f)
    # arm space (metres) = cr @ aim + ct  ->  aim = cr^T (arm - ct)
    array(out, 'float', 'kPitControllerR', cr.T.reshape(-1), 9, fmt_f)
    array(out, 'float', 'kPitControllerT', -(cr.T @ ct), 3, fmt_f)
    array(out, 'float', 'kPitControllerHandle', PZ.HANDLE_CENTRE + PZ.HANDLE_AXIS * PZ.HELD_SLIDE, 3, fmt_f)
    # the wrist panel's seat: on the two bands, midway between them
    be, bw = B.BAND_ELBOW, B.BAND_WRIST
    mid_e, mid_w = 0.5 * (be[0] + be[1]), 0.5 * (bw[0] + bw[1])
    out.append(f'constexpr float kPitSeatX = {fmt_f(0.5 * (mid_e + mid_w) / H.UNIT)};')
    out.append(f'constexpr float kPitSeatRadius = {fmt_f((be[2] * B.SQUASH[0] + 0.0006) / H.UNIT)};')
    out.append(f'constexpr float kPitSeatWidth = {fmt_f((be[2] * B.SQUASH[1] + 0.0006) / H.UNIT)};')
    out.append(f'constexpr float kPitSeatBands = {fmt_f(0.5 * (mid_w - mid_e) / H.UNIT)};')
    open(path, 'w').write('\n'.join(out) + '\n')
    log(f'{path}: {len(v)} vertices, {len(m["tris"])} triangles, {os.path.getsize(path) // 1024} KB')
    for name, first, count in ranges:
        log(f'  {name}: {count} triangles')


def write_laurel(path, log=print):
    v, n, tris, mat, shade, ranges = laurel_mesh()
    out = [HEADER]
    write_mesh(out, 'Laurel', v, n, tris, mat, shade)
    out.append('struct LaurelPartData { const char* name; uint8_t material; uint32_t first_index; uint32_t index_count; };')
    out.append(f'constexpr LaurelPartData kLaurelParts[{len(ranges)}] = {{')
    for name, first, count in ranges:
        out.append(f'    {{"{name}", {int(mat[tris[first][0]])}, {first * 3}, {count * 3}}},')
    out.append('};')
    out.append(f'constexpr float kLaurelPlateBack = {fmt_f(L.PLATE_Z[0])};')
    out.append(f'constexpr float kLaurelPlateHalfWidth = {fmt_f(L.HALF_W + L.PLATE_OUT)};')
    out.append(f'constexpr float kLaurelPlateHalfHeight = {fmt_f(L.HALF_H + L.PLATE_OUT)};')
    open(path, 'w').write('\n'.join(out) + '\n')
    log(f'{path}: {len(v)} vertices, {len(tris)} triangles, {os.path.getsize(path) // 1024} KB')
    for name, first, count in ranges:
        log(f'  {name}: {count} triangles')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(HERE, '..', '..', 'src', 'frame_xr', 'vis'))
    ap.add_argument('--cache')
    a = ap.parse_args()
    np.seterr(all='ignore')
    m, ranges = hand_mesh(a.cache)
    k = pose_keys(a.cache)
    write_hand(os.path.normpath(os.path.join(a.out, 'pit_hand_data.inc')), m, ranges, k)
    write_laurel(os.path.normpath(os.path.join(a.out, 'laurel_frame_data.inc')))


if __name__ == '__main__':
    main()
