#!/usr/bin/env python3
# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Makes Pit's drawn arm:

    python3 tools/kiu_hand/make_arm.py [--bin DIR]        (from the repository's root)

writes src/frame_xr/vis/pit_arm_data.inc (the mesh, the rig, the handle and the controller's
fit as C++ arrays) and src/frame_xr/vis/pit_arm_colour.png, pit_arm_surface.png (its two
images); with --bin also DIR/pit_hand.bin, the same model as the app's model file
(pit_model.h). Everything comes from the numbers in the arm_*.py files; no file is read. The
same numbers give the same bytes. See README.md."""
import argparse
import hashlib
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import arm_build as B     # noqa: E402
import arm_export as X    # noqa: E402
import arm_paint as PT    # noqa: E402
import arm_pose as AP     # noqa: E402
import arm_render as RD   # noqa: E402
import arm_skel as S      # noqa: E402

HEADER = '''// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Pit's drawn arm: made by tools/kiu_hand/make_arm.py from its own numbers (no game data, no
// picture). Do not edit: change the generator and run it again.
//
// A left forearm and hand in the arm's space (pit_hand.h), metres. Vertices: position
// (kPitArmPos * kPitArmPosScale + kPitArmPosMin), normal and tangent (over 127; the tangent's
// fourth is its handedness), uv (over 65535, v down) into pit_arm_colour.png and
// pit_arm_surface.png beside this file (pit_hand_shader.h says what they hold). Four joints a
// vertex (indices into the joints here) with weights summing to 255, falling. Triangles: the
// body first (both arms draw it; the right arm mirrored), then kPitArmPlateTriangleCount for the
// plate, which only the left arm wears. Joints as pit_model.h's file has them: a parent before
// its children, the role a PitHandJoint, flag 1 relative, the frame row major.
'''


def fmt_f(x):
    s = f'{float(x):.7g}'
    if 'e' not in s and '.' not in s:
        s += '.0'
    return s + 'f'


def array(out, ctype, name, values, per_line=24, fmt=str):
    vals = [fmt(v) for v in values]
    out.append(f'constexpr {ctype} {name}[{len(vals)}] = {{')
    for i in range(0, len(vals), per_line):
        out.append(','.join(vals[i:i + per_line]) + ',')
    out.append('};')


def write_inc(path, mesh, handle, fit_r, fit_t, pivot, size):
    out = [HEADER]
    v = mesh['v']
    lo, hi = v.min(0), v.max(0)
    scale = (hi - lo) / 65535.0
    q = np.clip(np.floor((v - lo) / scale + 0.5), 0, 65535).astype(int)
    nt = len(mesh['tris'])
    assert len(v) < 65536
    out.append(f'constexpr uint32_t kPitArmVertexCount = {len(v)};')
    out.append(f'constexpr uint32_t kPitArmTriangleCount = {nt};')
    out.append(f'constexpr uint32_t kPitArmPlateTriangleCount = {mesh["plate_tris"]};')
    out.append(f'constexpr uint32_t kPitArmTextureSize = {size};')
    array(out, 'float', 'kPitArmPosMin', lo, fmt=fmt_f)
    array(out, 'float', 'kPitArmPosScale', scale, fmt=fmt_f)
    array(out, 'uint16_t', 'kPitArmPos', q.reshape(-1))
    q8 = lambda a: np.clip(np.floor(a * 127 + 0.5), -127, 127).astype(int).reshape(-1)
    array(out, 'int8_t', 'kPitArmNormal', q8(mesh['n']), 32)
    array(out, 'int8_t', 'kPitArmTangent', q8(mesh['tan']), 32)
    array(out, 'uint16_t', 'kPitArmUv', np.clip(np.floor(mesh['uv'] * 65535 + 0.5), 0, 65535).astype(int).reshape(-1))
    wq, order = X.quantise_weights(mesh['weights'])
    bones = np.take_along_axis(np.vectorize(X.JOINT_INDEX.get)(mesh['bones']), order, 1)
    array(out, 'uint8_t', 'kPitArmBone', bones.reshape(-1), 48)
    array(out, 'uint8_t', 'kPitArmWeight', wq.reshape(-1), 32)
    array(out, 'uint16_t', 'kPitArmIndex', mesh['tris'].reshape(-1))
    js = X.FILE_JOINTS
    out.append(f'constexpr uint32_t kPitArmJointCount = {len(js)};')
    array(out, 'int8_t', 'kPitArmJointParent', [-1 if S.PARENT[j] < 0 else X.JOINT_INDEX[S.PARENT[j]] for j in js], 32)
    array(out, 'int8_t', 'kPitArmJointRole', js, 32)
    array(out, 'uint8_t', 'kPitArmJointFlags', [1 if j in S.RELATIVE else 0 for j in js], 32)
    array(out, 'float', 'kPitArmJointPosition', np.array([S.BIND_P[j] for j in js]).reshape(-1), 6, fmt_f)
    array(out, 'float', 'kPitArmJointFrame', np.array([S.BIND_R[j] for j in js]).reshape(-1), 9, fmt_f)
    array(out, 'float', 'kPitArmJointRadius', [S.RADIUS[j] for j in js], 8, fmt_f)
    import arm_gear as G
    out.append(f'constexpr float kPitArmHandLength = {fmt_f(S.FLAT_P[S.MIDDLE[4]][0])};')
    out.append(f'constexpr float kPitArmForearmRadius = {fmt_f(G.BAND_RO)};')
    out.append(f'constexpr float kPitArmWristRadius = {fmt_f(G.BAND_RO)};')
    array(out, 'float', 'kPitArmElbow', S.BIND_P[S.ELBOW], fmt=fmt_f)
    c, ax, rad = handle
    array(out, 'float', 'kPitArmHandle', list(c) + list(ax) + [rad], fmt=fmt_f)   # centre, axis, radius
    m = np.eye(4)
    m[:3, :3] = fit_r
    m[:3, 3] = fit_t
    array(out, 'float', 'kPitArmFit', m.T.reshape(-1), 4, fmt_f)                    # column major
    array(out, 'float', 'kPitArmFitPivot', pivot, fmt=fmt_f)
    with open(path, 'w') as f:
        f.write('\n'.join(out) + '\n')


def build(log=print):
    mesh = B.assemble(log=log)
    colour, surface = PT.paint(mesh, log=log)
    handle, fit_r, fit_t, pivot = AP.controller_fit()
    return mesh, colour, surface, handle, fit_r, fit_t, pivot


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(HERE, '..', '..', 'src', 'frame_xr', 'vis'))
    ap.add_argument('--bin', help='also write DIR/pit_hand.bin (the app\'s model file)')
    a = ap.parse_args()
    np.seterr(all='ignore')
    mesh, colour, surface, handle, fit_r, fit_t, pivot = build()
    out = os.path.normpath(a.out)
    write_inc(os.path.join(out, 'pit_arm_data.inc'), mesh, handle, fit_r, fit_t, pivot, colour.shape[0])
    RD.write_png(os.path.join(out, 'pit_arm_colour.png'), colour)
    RD.write_png(os.path.join(out, 'pit_arm_surface.png'), surface)
    data = X.pack(mesh, colour, surface, handle, fit_r, fit_t, pivot)
    read = X.check(data)
    files = [os.path.join(out, n) for n in ('pit_arm_data.inc', 'pit_arm_colour.png', 'pit_arm_surface.png')]
    if a.bin:
        os.makedirs(a.bin, exist_ok=True)
        files.append(os.path.join(a.bin, 'pit_hand.bin'))
        with open(files[-1], 'wb') as f:
            f.write(data)
    print(f'{len(read["vertices"])} vertices, {(read["body"] + read["plate"]) // 3} triangles '
          f'({read["plate"] // 3} the plate\'s), {len(read["joints"])} joints, images {colour.shape[1]}x{colour.shape[0]}; '
          f'the model file checks ({len(data)} bytes)')
    for p in files:
        print(f'  {hashlib.sha256(open(p, "rb").read()).hexdigest()[:16]}  {os.path.getsize(p):>8}  {p}')


if __name__ == '__main__':
    main()
