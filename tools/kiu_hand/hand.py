# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Pit's hand: our own left forearm and hand for Kid Icarus: Uprising's first person in VR
(src/frame_xr/vis/pit_hand.h). Everything here is made by this script from the numbers typed
below: no game mesh, texture or artwork is read. numpy only.

Space (the arm's, as src/frame_xr/vis/weapon_fit.h): the wrist at the origin, the forearm toward
-x, the fingers +x, the back of the hand +y, the thumb +z. Worked out in metres here and written
in arm units (UNIT metres each, weapon_fit.h kArmScale), so the fits there place it unchanged.

The skeleton is OpenXR's hand joint set (XR_HAND_JOINT_*_EXT, 26 joints: -Z toward the finger
tip, +Y the back of the hand or finger, +X to the right) for a left hand, plus the forearm (at
the wrist, -Z toward the elbow... see FOREARM) and the elbow.
"""
import numpy as np

UNIT = 0.075  # metres per arm unit (weapon_fit.h kArmScale)

# ---- joints ----
PALM, WRIST = 0, 1
THUMB = (2, 3, 4, 5)            # metacarpal, proximal, distal, tip
INDEX = (6, 7, 8, 9, 10)        # metacarpal, proximal, intermediate, distal, tip
MIDDLE = (11, 12, 13, 14, 15)
RING = (16, 17, 18, 19, 20)
LITTLE = (21, 22, 23, 24, 25)
FOREARM, ELBOW = 26, 27         # ours: the forearm (at the wrist) and its far end
NJ = 28
FINGERS = (INDEX, MIDDLE, RING, LITTLE)
JOINT_NAMES = ['palm', 'wrist', 'thumb_metacarpal', 'thumb_proximal', 'thumb_distal', 'thumb_tip'] + [
    f'{f}_{p}' for f in ('index', 'middle', 'ring', 'little')
    for p in ('metacarpal', 'proximal', 'intermediate', 'distal', 'tip')] + ['forearm', 'elbow']
PARENT = [WRIST, -1, WRIST, 2, 3, 4] + sum(([WRIST, c[0], c[1], c[2], c[3]] for c in FINGERS), []) + [-1, FOREARM]

ELBOW_X = -0.164  # the forearm's far end (the game's arm ends 16.4 cm from the wrist at this scale)


def norm(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def rot_x(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_y(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rot_z(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def frame(direction, up):
    """A joint's axes (columns X, Y, Z) with -Z along `direction` and +Y nearest `up`."""
    z = -norm(direction)
    x = norm(np.cross(up, z))
    y = np.cross(z, x)
    return np.stack([x, y, z], 1)


def quat_from_mat(m):
    """(x, y, z, w) of a rotation matrix."""
    t = np.trace(m)
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
    q = np.array(q)
    return q / np.linalg.norm(q) * (1 if q[3] >= 0 else -1)


def mat_from_quat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


# ---- the flat hand: every finger straight, the proportions (metres) ----
# Stylised: a youthful hand a little broad in the palm, the fingers full and softly tapered.
UP = np.array([0.0, 1.0, 0.0])
#           metacarpal base        knuckle (proximal joint)   yaw (deg, +: toward the thumb)  lengths
FINGER_DEF = {
    INDEX:  ((0.024, 0.001, 0.015), (0.0890, 0.0015, 0.0265), 3.0, (0.0400, 0.0240, 0.0215)),
    MIDDLE: ((0.025, 0.002, 0.003), (0.0930, 0.0030, 0.0045), 0.0, (0.0440, 0.0270, 0.0225)),
    RING:   ((0.024, 0.001, -0.008), (0.0885, 0.0010, -0.0165), -3.0, (0.0410, 0.0250, 0.0215)),
    LITTLE: ((0.022, -0.001, -0.018), (0.0790, -0.0025, -0.0355), -6.0, (0.0320, 0.0190, 0.0190)),
}
#         radii at: knuckle, middle joint, last joint, tip
FINGER_R = {
    INDEX: (0.0105, 0.0098, 0.0092, 0.0083),
    MIDDLE: (0.0108, 0.0101, 0.0094, 0.0085),
    RING: (0.0103, 0.0096, 0.0090, 0.0081),
    LITTLE: (0.0093, 0.0087, 0.0081, 0.0074),
}
THUMB_POS = [(0.020, -0.011, 0.021), (0.050, -0.017, 0.045), (0.079, -0.021, 0.060), (0.103, -0.024, 0.069)]
THUMB_R = (0.0130, 0.0114, 0.0103, 0.0088)
THUMB_UP = norm([0.0, 0.72, 0.69])  # the nail's side: up and out


def flat_skeleton():
    """World positions (NJ, 3) and axes (NJ, 3, 3) of the flat hand."""
    p = np.zeros((NJ, 3))
    r = np.zeros((NJ, 3, 3))
    r[WRIST] = frame([1, 0, 0], UP)
    p[PALM] = (0.058, 0.003, 0.004)
    r[PALM] = r[WRIST]
    for chain, (base, knuckle, yaw, lens) in FINGER_DEF.items():
        base, knuckle = np.array(base), np.array(knuckle)
        d = rot_y(-np.radians(yaw)) @ np.array([1.0, 0, 0])
        p[chain[0]] = base
        r[chain[0]] = frame(knuckle - base, UP)
        q = knuckle
        for i in range(3):
            p[chain[1 + i]] = q
            r[chain[1 + i]] = frame(d, UP)
            q = q + d * lens[i]
        p[chain[4]] = q
        r[chain[4]] = r[chain[3]]
    tp = [np.array(t) for t in THUMB_POS]
    for i in range(3):
        p[THUMB[i]] = tp[i]
        r[THUMB[i]] = frame(tp[i + 1] - tp[i], THUMB_UP)
    p[THUMB[3]] = tp[3]
    r[THUMB[3]] = r[THUMB[2]]
    r[FOREARM] = frame([1, 0, 0], UP)  # as the wrist: -Z toward the hand, the elbow at +Z
    p[ELBOW] = (ELBOW_X, 0, 0)
    r[ELBOW] = r[FOREARM]
    return p, r


FLAT_P, FLAT_R = flat_skeleton()
# Each joint in its parent's flat frame: the bone offsets (ours, never changed by a pose) and
# the flat local rotations.
LOCAL_T = np.zeros((NJ, 3))
FLAT_LOCAL = np.zeros((NJ, 3, 3))
for _j in range(NJ):
    if PARENT[_j] < 0:
        LOCAL_T[_j] = FLAT_P[_j]
        FLAT_LOCAL[_j] = FLAT_R[_j]
    else:
        LOCAL_T[_j] = FLAT_R[PARENT[_j]].T @ (FLAT_P[_j] - FLAT_P[PARENT[_j]])
        FLAT_LOCAL[_j] = FLAT_R[PARENT[_j]].T @ FLAT_R[_j]


def fk(local):
    """World positions and axes from local rotations (NJ, 3, 3)."""
    p = np.zeros((NJ, 3))
    r = np.zeros((NJ, 3, 3))
    for j in [WRIST, PALM] + list(range(2, 26)) + [FOREARM, ELBOW]:
        if PARENT[j] < 0:
            p[j], r[j] = LOCAL_T[j], local[j]
        else:
            p[j] = p[PARENT[j]] + r[PARENT[j]] @ LOCAL_T[j]
            r[j] = r[PARENT[j]] @ local[j]
    return p, r


# ---- poses: angles in degrees ----
# Each finger: (spread, knuckle flex, middle flex, last flex); spread + is toward the thumb.
# The thumb: (metacarpal flex, metacarpal spread, metacarpal twist, proximal flex, distal flex,
# proximal spread): flex curls it toward the palm, spread + swings it away from the index finger.
def pose_local(fingers, thumb, cup=0.0):
    """Local rotations for a pose. `cup`: degrees the ring and little metacarpals fold in."""
    local = FLAT_LOCAL.copy()
    for fi, chain in enumerate(FINGERS):
        spread, a, b, c = fingers[fi]
        local[chain[1]] = FLAT_LOCAL[chain[1]] @ rot_y(-np.radians(spread)) @ rot_x(-np.radians(a))
        local[chain[2]] = FLAT_LOCAL[chain[2]] @ rot_x(-np.radians(b))
        local[chain[3]] = FLAT_LOCAL[chain[3]] @ rot_x(-np.radians(c))
        if cup and fi >= 2:
            local[chain[0]] = FLAT_LOCAL[chain[0]] @ rot_x(-np.radians(cup * (0.5 if fi == 2 else 1.0)))
    mf, ms, mt, pf, df, ps = thumb
    local[THUMB[0]] = (FLAT_LOCAL[THUMB[0]] @ rot_y(-np.radians(ms)) @ rot_x(-np.radians(mf)) @
                       rot_z(np.radians(mt)))
    local[THUMB[1]] = FLAT_LOCAL[THUMB[1]] @ rot_y(-np.radians(ps)) @ rot_x(-np.radians(pf))
    local[THUMB[2]] = FLAT_LOCAL[THUMB[2]] @ rot_x(-np.radians(df))
    return local


# The bind pose: the hand the mesh is made in: open, the fingers a little apart and barely
# curled, so every joint bends well both ways and no two fingers touch.
BIND_FINGERS = [(7.0, 6.0, 9.0, 6.0), (1.0, 6.0, 9.0, 6.0), (-6.0, 6.0, 9.0, 6.0), (-13.0, 6.0, 9.0, 6.0)]
BIND_THUMB = (0.0, 4.0, 0.0, 4.0, 6.0, 0.0)
BIND_LOCAL = pose_local(BIND_FINGERS, BIND_THUMB)
BIND_P, BIND_R = fk(BIND_LOCAL)
