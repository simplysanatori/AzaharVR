# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Pit's arm, drawn: written as the app's model file (src/frame_xr/vis/pit_model.h, "PITHAND1")
and read back and checked here against every rule that header states, since the app's own
loader is not run by this tool."""
import struct

import numpy as np

import arm_gear as G
import arm_skel as S

MAGIC = b'PITHAND1'
FILE_JOINTS = list(S.ORDER)                      # the file wants a parent before its children
JOINT_INDEX = {j: i for i, j in enumerate(FILE_JOINTS)}


def quantise_weights(w):
    """(N, 4) weights summing to 1, falling, as bytes summing to 255."""
    q = np.floor(w * 255.0).astype(int)
    left = 255 - q.sum(1)
    frac = w * 255.0 - q
    order = np.argsort(-frac, axis=1, kind='stable')
    for k in range(4):
        add = (left > k).astype(int)
        np.add.at(q, (np.arange(len(q)), order[:, k]), add)
    # keep them falling
    idx = np.argsort(-q, axis=1, kind='stable')
    return np.take_along_axis(q, idx, 1).astype(np.uint8), idx


def pack(mesh, colour, surface, handle, fit_r, fit_t, pivot):
    """The file's bytes. `mesh` from arm_build.assemble (metres), images (H, W, 4) uint8."""
    v = mesh['v']
    nv = len(v)
    tris = mesh['tris']
    plate = mesh['plate_tris']
    h, w = colour.shape[:2]
    out = [MAGIC, struct.pack('<8I', 1, nv, (len(tris) - plate) * 3, plate * 3, len(FILE_JOINTS), w, h, 0)]
    hand_length = float(S.FLAT_P[S.MIDDLE[4]][0] - S.FLAT_P[S.WRIST][0])
    out.append(struct.pack('<4f', hand_length, G.BAND_RO, G.BAND_RO, 0.0))
    c, ax, rad = handle
    out.append(struct.pack('<3f', *S.BIND_P[S.ELBOW]))
    out.append(struct.pack('<7f', *c, *ax, rad))
    m = np.eye(4)
    m[:3, :3] = fit_r
    m[:3, 3] = fit_t
    out.append(struct.pack('<16f', *m.T.reshape(-1)))     # column major
    out.append(struct.pack('<3f', *pivot))
    for j in FILE_JOINTS:
        parent = -1 if S.PARENT[j] < 0 else JOINT_INDEX[S.PARENT[j]]
        out.append(struct.pack('<iiI', parent, j, 1 if j in S.RELATIVE else 0))
        out.append(struct.pack('<3f', *S.BIND_P[j]))
        out.append(struct.pack('<9f', *S.BIND_R[j].reshape(-1)))   # row major; its columns are the axes
        out.append(struct.pack('<f', float(S.RADIUS[j])))
    vert = np.concatenate([v, mesh['n'], mesh['tan'], mesh['uv']], 1).astype('<f4')
    out.append(vert.tobytes())
    wq, order = quantise_weights(mesh['weights'])
    bones = np.take_along_axis(np.vectorize(JOINT_INDEX.get)(mesh['bones']), order, 1).astype(np.uint8)
    out.append(np.concatenate([bones, wq], 1).tobytes())
    out.append(tris.astype('<u4').tobytes())
    out.append(colour.tobytes())
    out.append(surface.tobytes())
    return b''.join(out)


def check(data):
    """Reads the file's bytes back and checks them as pit_model.h says a loader must: sizes,
    counts, indices, numbers. Returns what it read; raises on a fault."""
    assert data[:8] == MAGIC, 'magic'
    at = 8
    ver, nv, nbody, nplate, nj, w, h, zero = struct.unpack_from('<8I', data, at)
    at += 32
    assert ver == 1 and zero == 0
    assert 0 < nv <= 200000 and (nbody + nplate) <= 1200000 and 0 < nj <= 255 and 0 < w <= 4096 and 0 < h <= 4096
    assert nbody % 3 == 0 and nplate % 3 == 0
    hand_length, fr, wr, z = struct.unpack_from('<4f', data, at)
    at += 16
    assert hand_length > 0 and fr > 0 and wr > 0 and z == 0
    elbow = struct.unpack_from('<3f', data, at)
    at += 12
    handle = struct.unpack_from('<7f', data, at)
    at += 28
    assert abs(np.linalg.norm(handle[3:6]) - 1) < 1e-4 and handle[6] > 0
    fit = np.array(struct.unpack_from('<16f', data, at)).reshape(4, 4).T
    at += 64
    assert np.allclose(fit[3], [0, 0, 0, 1]) and np.allclose(fit[:3, :3] @ fit[:3, :3].T, np.eye(3), atol=1e-4)
    pivot = struct.unpack_from('<3f', data, at)
    at += 12
    joints = []
    roles = set()
    for i in range(nj):
        parent, role, flags = struct.unpack_from('<iiI', data, at)
        at += 12
        pos = struct.unpack_from('<3f', data, at)
        at += 12
        frame = np.array(struct.unpack_from('<9f', data, at)).reshape(3, 3)
        at += 36
        radius, = struct.unpack_from('<f', data, at)
        at += 4
        assert -1 <= parent < i, 'a parent comes first'
        assert -1 <= role < S.NJ and role not in roles and flags in (0, 1) and radius >= 0
        if role >= 0:
            roles.add(role)
        assert np.allclose(frame @ frame.T, np.eye(3), atol=1e-4) and np.linalg.det(frame) > 0
        joints.append((parent, role, flags, pos, frame, radius))
    vert = np.frombuffer(data, '<f4', nv * 12, at).reshape(nv, 12)
    at += nv * 48
    assert np.isfinite(vert).all()
    assert np.allclose(np.linalg.norm(vert[:, 3:6], axis=1), 1, atol=1e-3), 'normals'
    assert np.allclose(np.linalg.norm(vert[:, 6:9], axis=1), 1, atol=1e-3), 'tangents'
    assert (np.abs(vert[:, 9]) == 1).all() and (vert[:, 10:] >= 0).all() and (vert[:, 10:] <= 1).all()
    bw = np.frombuffer(data, np.uint8, nv * 8, at).reshape(nv, 8)
    at += nv * 8
    assert (bw[:, :4] < nj).all() and (bw[:, 4:].astype(int).sum(1) == 255).all()
    assert (np.diff(bw[:, 4:].astype(int), axis=1) <= 0).all(), 'weights fall'
    idx = np.frombuffer(data, '<u4', nbody + nplate, at)
    at += (nbody + nplate) * 4
    assert (idx < nv).all()
    colour = np.frombuffer(data, np.uint8, w * h * 4, at).reshape(h, w, 4)
    at += w * h * 4
    surface = np.frombuffer(data, np.uint8, w * h * 4, at).reshape(h, w, 4)
    at += w * h * 4
    assert at == len(data), 'no bytes left over'
    return dict(vertices=vert, bones=bw[:, :4], weights=bw[:, 4:], indices=idx, body=nbody, plate=nplate, joints=joints,
                colour=colour, surface=surface, hand_length=hand_length, elbow=elbow, handle=handle, fit=fit, pivot=pivot)
