# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Pit's arm, drawn: posed as the app poses it, for the pictures and the checks. The poses are
the module's (src/frame_xr/vis/pit_hand.cpp: its key poses, solved by poses.py, blended by the
controller's touches and pulls as PitHandPoseFromCurls does), put on this arm's joints
(arm_skel.to_arm), then, with the controller's handle in the palm, each finger opened until it
lies on the handle (pit_model.h says the app does so; its code is not read here, so this is
this script's own version of that rule, and the app may differ a little).

Also where the handle lies in this arm's palm and the fit onto the Steam Frame controller,
which go into the model file."""
import numpy as np

import arm_shape as A
import arm_skel as S
import hand as H
import poses as PZ

PRESS = 0.92          # pit_model.h kPitModelPress: how much of a finger's thickness may sink in
GRIP_X = 0.0660       # where the handle crosses the hand: under the knuckles
GRIP_TILT_DEG = 30.0
HELD_SLIDE = 0.0      # along the handle: chosen with the tilt so the index pad is nearest the trigger
HELD_ROLL_DEG = 0.0


def handle_in_palm(radius=PZ.HANDLE_RADIUS, x0=GRIP_X, tilt_deg=GRIP_TILT_DEG):
    """The handle's centre and axis in the arm's space: as close to the palm as it goes."""
    t = np.radians(tilt_deg)
    axis = np.array([np.sin(t), 0.0, np.cos(t)])
    ang = np.linspace(0, 2 * np.pi, 36, endpoint=False)
    along = np.linspace(-0.040, 0.026, 14)
    side = np.cross([0.0, 1.0, 0.0], axis)
    ring = np.array([np.cos(a) * np.array([0.0, 1.0, 0.0]) + np.sin(a) * side for a in ang])
    pts = (ring[None] * radius + (along[:, None] * axis)[:, None]).reshape(-1, 3)
    lo, hi = -0.09, 0.0
    for _ in range(30):
        y = 0.5 * (lo + hi)
        if A.palm_field(pts + np.array([x0, y, 0.0])).min() < -0.0015:   # flesh gives a little
            hi = y
        else:
            lo = y
    return np.array([x0, lo, 0.0]), axis


def controller_fit():
    """(handle centre, axis, radius), and the arm's space to the left controller's aim space:
    aim = r @ arm + t (metres), with the pivot a scale keeps."""
    c, ax = handle_in_palm()
    cr, ct = PZ.controller_fit(c, ax, HELD_ROLL_DEG, HELD_SLIDE)
    return (c, ax, PZ.HANDLE_RADIUS), cr.T, -(cr.T @ ct), PZ.HANDLE_CENTRE + PZ.HANDLE_AXIS * HELD_SLIDE


# ---- the module's poses ----
THUMB_J = list(H.THUMB[:3])
INDEX_J = list(H.INDEX[:4])
REST_J = [list(H.MIDDLE[:4]), list(H.RING[:4]), list(H.LITTLE[:4])]
ZERO = [(0, 0, 0, 0)] * 4


def nlerp(a, b, t):
    qa, qb = H.quat_from_mat(a), H.quat_from_mat(b)
    if qa @ qb < 0:
        qb = -qb
    q = qa * (1 - t) + qb * t
    return H.mat_from_quat(q / np.linalg.norm(q))


class Module:
    """The module's key poses and its blend of them."""

    def __init__(self, keys):
        self.thumb, self.index, self.rest = keys[0], keys[1], keys[2]

    def thumb_key(self, k):
        return H.pose_local(ZERO, self.thumb[k], 0)

    def index_key(self, k):
        return H.pose_local([self.index[k]] + ZERO[1:], H.BIND_THUMB, 0)

    def rest_key(self, k):
        return H.pose_local([ZERO[0]] + list(self.rest[k][0]), H.BIND_THUMB, self.rest[k][1])

    def from_curls(self, index, rest, thumb, thumb_on='rest', held=True):
        """PitHandPoseFromCurls: `rest` the middle, ring and little fingers' curls."""
        local = H.FLAT_LOCAL.copy()
        i0, i1, i2 = ('held_off', 'held_touch', 'held_pull') if held else ('point', 'relaxed', 'fist')
        a, b, t = (i0, i1, index * 2) if index < 0.5 else (i1, i2, index * 2 - 1)
        ka, kb = self.index_key(a), self.index_key(b)
        for j in INDEX_J:
            local[j] = nlerp(ka[j], kb[j], t)
        r0, r1 = ('held_open', 'held') if held else ('relaxed', 'fist')
        ka, kb = self.rest_key(r0), self.rest_key(r1)
        for f, js in enumerate(REST_J):
            for j in js:
                local[j] = nlerp(ka[j], kb[j], rest[f])
        closed = sum(rest) / 3.0
        if held:
            ka, kb = self.thumb_key('held_up'), self.thumb_key({'stick': 'held_stick', 'buttons': 'held_buttons'}.get(thumb_on, 'held_rest'))
            for j in THUMB_J:
                local[j] = nlerp(ka[j], kb[j], thumb)
        else:
            o, u, rl, fs = (self.thumb_key(k) for k in ('open', 'up', 'relaxed', 'fist'))
            for j in THUMB_J:
                local[j] = nlerp(nlerp(o[j], u[j], closed), nlerp(rl[j], fs[j], closed), thumb)
        return local

    def from_inputs(self, trigger=0.0, trigger_touch=False, grip=0.0, grip_touch=False, thumb=None, held=True):
        """PitHandCurlsFromInputs then from_curls. `thumb`: None (lifted), 'rest', 'stick' or
        'buttons'."""
        index = 0.5 + 0.5 * trigger if trigger_touch else 0.5 * trigger
        rest = 0.7 + 0.3 * grip if grip_touch else grip
        return self.from_curls(index, [rest] * 3, 0.0 if thumb is None else 1.0, thumb or 'rest', held)

    def named(self, name):
        fingers = {'open': PZ.OPEN, 'relaxed': PZ.RELAXED}[name]
        return H.pose_local(fingers[0], fingers[1], fingers[2])


def capsules(p):
    out = {}
    for f, c in enumerate(S.FINGERS):
        r = S.FINGER_R[c]
        pts = [p[j] for j in c[1:]]
        end = pts[3] - S.norm(pts[3] - pts[2]) * r[3]
        out[f] = [(pts[0], pts[1], r[0], r[1]), (pts[1], pts[2], r[1], r[2]), (pts[2], end, r[2], r[3])]
    r = S.THUMB_R
    pts = [p[j] for j in S.THUMB]
    end = pts[3] - S.norm(pts[3] - pts[2]) * r[3]
    out['thumb'] = [(pts[1], pts[2], r[1], r[2]), (pts[2], end, r[2], r[3])]
    return out


def sink(cap, handle):
    """How far a bone's flesh is inside the handle, past what it may press in."""
    c, ax, rad = handle
    worst = 0.0
    for t in np.linspace(0, 1, 7):
        q = cap[0] + (cap[1] - cap[0]) * t - c
        d = np.linalg.norm(q - ax * (q @ ax))
        worst = max(worst, rad + (cap[2] + (cap[3] - cap[2]) * t) * (1.0 - PRESS) - d)
    return worst


def on_arm(local, handle=None, elbow=None):
    """The module's pose on this arm: joints' places and axes. `handle`: the controller's handle
    is in the palm: a finger (or the thumb) that would close through it is opened until it lies
    on it."""
    if handle is not None:
        local = local.copy()
        groups = [(0, INDEX_J)] + [(f + 1, js) for f, js in enumerate(REST_J)] + [('thumb', THUMB_J)]
        for which, js in groups:
            want = {j: local[j].copy() for j in js}
            for s in np.linspace(1.0, 0.0, 41):
                for j in js:
                    local[j] = nlerp(H.FLAT_LOCAL[j], want[j], s)
                p, _ = S.to_arm(local)
                if max(sink(c, handle) for c in capsules(p)[which]) <= 0.0:
                    break
    return S.to_arm(local, elbow)
