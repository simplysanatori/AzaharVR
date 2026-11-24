# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Pit's arm, drawn: its own skeleton. The joints are the module's (hand.py: OpenXR's 26, the
forearm and the elbow, the same order, axes and parents), the bones' places and lengths are this
arm's: a short broad palm, full fingers. Everything is typed here; nothing is read.

The app poses this arm with the module's poses (pit_model.h): a joint takes the module joint's
own orientation, a joint marked relative only its turn from the module's flat hand, and every
bone keeps this file's length. `to_arm` does the same here, for the pictures and the checks."""
import numpy as np

import hand as H
from hand import frame, norm, rot_x, rot_y, rot_z

NJ = H.NJ
PARENT = H.PARENT
PALM, WRIST, FOREARM, ELBOW = H.PALM, H.WRIST, H.FOREARM, H.ELBOW
THUMB, INDEX, MIDDLE, RING, LITTLE = H.THUMB, H.INDEX, H.MIDDLE, H.RING, H.LITTLE
FINGERS = H.FINGERS
ORDER = [WRIST, PALM] + list(range(2, 26)) + [FOREARM, ELBOW]  # parents first

# ---- proportions ----
# Over the palm's length PL (wrist to the middle knuckle): fingers index 1.03, middle 1.05,
# ring 1.02, little 0.81, the thumb 1.40 from its root; the hand as long as the forearm.
# The thicknesses asked for (base/middle/tip: index .47/.43/.31, middle .53/.42/.34, ring
# .46/.37/.34, little .46/.33/.25, thumb .62/.42/.38) do not close as widths: the four bases sum
# to 1.92 against a palm 1.60 wide with the thumb's base. They are read here as relative: kept in
# their ratios (finger to finger, base to tip) and scaled by 0.72 so that four fingers side by
# side are as wide as the knuckles (1.31 PL), which is what the pictures show.
PL = 0.080
ELBOW_X = -0.164          # hand length = PL * 2.05 = forearm length
UP = np.array([0.0, 1.0, 0.0])
#           metacarpal base          knuckle                    yaw    lengths
FINGER_DEF = {
    INDEX:  ((0.022, 0.000, 0.021), (0.0780, 0.0030, 0.0390), 3.0, (0.0387, 0.0239, 0.0198)),
    MIDDLE: ((0.023, 0.001, 0.006), (0.0800, 0.0040, 0.0125), 0.0, (0.0395, 0.0244, 0.0201)),
    RING:   ((0.022, 0.000, -0.008), (0.0760, 0.0030, -0.0140), -3.0, (0.0384, 0.0236, 0.0196)),
    LITTLE: ((0.020, -0.002, -0.022), (0.0670, 0.0000, -0.0390), -6.0, (0.0300, 0.0188, 0.0160)),
}
#         half thickness at: knuckle, middle joint, last joint, tip
FINGER_R = {
    INDEX: (0.0135, 0.0124, 0.0110, 0.0098),
    MIDDLE: (0.0146, 0.0122, 0.0112, 0.0102),
    RING: (0.0132, 0.0112, 0.0105, 0.0099),
    LITTLE: (0.0130, 0.0100, 0.0089, 0.0080),
}
THUMB_ROOT = np.array([0.014, -0.010, 0.028])
THUMB_DIRS = [(0.74, -0.14, 0.66), (0.86, -0.12, 0.49), (0.92, -0.12, 0.37)]
THUMB_LENS = (0.0440, 0.0370, 0.0310)
THUMB_R = (0.0176, 0.0134, 0.0124, 0.0114)
THUMB_UP = norm([0.0, 0.72, 0.69])
PALM_R = 0.0125  # half the palm's thickness, for the metacarpals


def flat_skeleton():
    p = np.zeros((NJ, 3))
    r = np.zeros((NJ, 3, 3))
    r[WRIST] = frame([1, 0, 0], UP)
    p[PALM] = (0.050, 0.002, 0.002)
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
    q = THUMB_ROOT.copy()
    for i in range(3):
        p[THUMB[i]] = q
        r[THUMB[i]] = frame(THUMB_DIRS[i], THUMB_UP)
        q = q + norm(THUMB_DIRS[i]) * THUMB_LENS[i]
    p[THUMB[3]] = q
    r[THUMB[3]] = r[THUMB[2]]
    r[FOREARM] = frame([1, 0, 0], UP)
    p[ELBOW] = (ELBOW_X, 0, 0)
    r[ELBOW] = r[FOREARM]
    return p, r


FLAT_P, FLAT_R = flat_skeleton()
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
    p = np.zeros((NJ, 3))
    r = np.zeros((NJ, 3, 3))
    for j in ORDER:
        if PARENT[j] < 0:
            p[j], r[j] = LOCAL_T[j], local[j]
        else:
            p[j] = p[PARENT[j]] + r[PARENT[j]] @ LOCAL_T[j]
            r[j] = r[PARENT[j]] @ local[j]
    return p, r


def pose_local(fingers, thumb, cup=0.0):
    """hand.py's pose_local on this arm's flat hand."""
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


# The bind pose: open, the fingers apart and barely curled; the thumb out to the side.
BIND_FINGERS = [(8.0, 6.0, 9.0, 6.0), (1.0, 6.0, 9.0, 6.0), (-6.5, 6.0, 9.0, 6.0), (-14.0, 6.0, 9.0, 6.0)]
BIND_THUMB = (0.0, 6.0, 0.0, 4.0, 6.0, 0.0)
BIND_LOCAL = pose_local(BIND_FINGERS, BIND_THUMB)
BIND_P, BIND_R = fk(BIND_LOCAL)

# Joints that take only their turn from the module's flat hand (pit_model.h: relative): the palm
# and the four fingers' metacarpals, so the palm keeps this arm's own shape.
RELATIVE = {PALM} | {c[0] for c in FINGERS}


def radius_table():
    r = np.zeros(NJ)
    for c in FINGERS:
        r[c[0]] = PALM_R
        for j, x in zip(c[1:], FINGER_R[c]):
            r[j] = x
    for j, x in zip(THUMB, THUMB_R):
        r[j] = x
    return r


RADIUS = radius_table()


def to_arm(module_local, elbow=None):
    """The module's pose (its local rotations, hand.py) on this arm: every joint's place and
    axes (NJ, 3), (NJ, 3, 3), as pit_model.h says the app does it."""
    _, mr = H.fk(module_local)
    p = np.zeros((NJ, 3))
    r = np.zeros((NJ, 3, 3))
    for j in ORDER:
        if j in (FOREARM, ELBOW):
            r[j] = BIND_R[j]
        elif j in RELATIVE:
            r[j] = mr[j] @ H.FLAT_R[j].T @ BIND_R[j]
        else:
            r[j] = mr[j]
        if PARENT[j] < 0:
            p[j] = BIND_P[j]
        else:
            pa = PARENT[j]
            p[j] = p[pa] + r[pa] @ (BIND_R[pa].T @ (BIND_P[j] - BIND_P[pa]))
    if elbow is not None:
        a = norm(BIND_P[ELBOW] - BIND_P[FOREARM])
        b = norm(np.asarray(elbow, float) - p[FOREARM])
        v = np.cross(a, b)
        c = float(a @ b)
        k = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
        turn = np.eye(3) + k + k @ k / (1 + c) if c > -0.999 else np.eye(3)
        r[FOREARM] = turn @ BIND_R[FOREARM]
        r[ELBOW] = r[FOREARM]
        p[ELBOW] = p[FOREARM] + r[FOREARM] @ (BIND_R[FOREARM].T @ (BIND_P[ELBOW] - BIND_P[FOREARM]))
    return p, r


def bone_matrices(p, r):
    """Bind pose to the pose: (NJ, 3, 4)."""
    m = np.zeros((NJ, 3, 4))
    for j in range(NJ):
        rot = r[j] @ BIND_R[j].T
        m[j, :, :3] = rot
        m[j, :, 3] = p[j] - rot @ BIND_P[j]
    return m


def skin(v, n, bones, weights, mats):
    """Linear blend skinning of positions and normals (bones (N, 4) ints, weights (N, 4))."""
    pv = np.zeros_like(v)
    pn = np.zeros_like(n)
    for k in range(4):
        m = mats[bones[:, k]]
        w = weights[:, k:k + 1]
        pv += w * (np.einsum('nij,nj->ni', m[:, :, :3], v) + m[:, :, 3])
        pn += w * np.einsum('nij,nj->ni', m[:, :, :3], n)
    return pv, norm(pn)
