# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Pit's hand: the poses, as joint angles (hand.py pose_local), and how it sits on the Steam
Frame controller. The free poses (no controller drawn) are typed in; the ones that hold the
handle are solved here against the handle's cylinder and the places of the trigger and the
stick, so the fingers close on them without passing through.

The controller's numbers are measurements of its model in its OpenXR aim space (as
weapon_fit.h kHandleAxis and kHandleCentre are): the right one's, mirrored for our left hand."""
import numpy as np

import hand as H
from hand import norm

# ---- the controller (left: the right one's x turned over), aim space, metres ----
HANDLE_AXIS = norm([0.0550, 0.7387, -0.6718])      # weapon_fit.h kHandleAxis, mirrored
HANDLE_CENTRE = np.array([-0.013, -0.0444, 0.0871])  # weapon_fit.h kHandleCentre, mirrored
HANDLE_RADIUS = 0.0180                              # the handle's section where the fist is
TRIGGER_FRONT = np.array([-0.0055, -0.0395, 0.0100])  # the middle of the trigger's face
TRIGGER_TRAVEL = np.array([0.0, 0.0045, 0.0085])    # where a full pull takes that point
STICK_TOP = np.array([0.0089, 0.0113, 0.0401])
BUTTONS_TOP = np.array([-0.0220, 0.0095, 0.0350])   # between the two face buttons
THUMB_REST = np.array([-0.0060, 0.0080, 0.0560])    # the top face behind the stick
TOP_NORMAL = norm([0.0, 0.92, 0.39])                # the top face's outward normal

# ---- the free poses ----
#            spread, knuckle, middle, last  (index, middle, ring, little)
OPEN = ([(9, -2, 3, 2), (1, -3, 3, 2), (-7, -2, 3, 2), (-16, 0, 4, 3)], (-6, 16, 0, -4, -2, 0), 0)
RELAXED = ([(4, 20, 30, 16), (0, 27, 36, 18), (-3, 33, 40, 20), (-8, 38, 44, 22)], (10, 2, 4, 14, 20, 0), 3)
FIST_FINGERS = [(-2, 84, 100, 60), (0, 86, 102, 60), (2, 88, 102, 60), (5, 90, 100, 58)]
POINT_INDEX = (1, 3, 6, 4)


def capsules(p, r):
    """The bones as rounded cones in a pose: [(a, b, ra, rb, joint)] for every phalanx, the
    thumb's metacarpal and the palm's metacarpals."""
    out = []
    for c in H.FINGERS:
        rr = H.FINGER_R[c]
        pts = [p[j] for j in c[1:]]
        tip_end = pts[3] - norm(pts[3] - pts[2]) * rr[3]
        out.append((pts[0], pts[1], rr[0], rr[1], c[1]))
        out.append((pts[1], pts[2], rr[1], rr[2], c[2]))
        out.append((pts[2], tip_end, rr[2], rr[3], c[3]))
    rr = H.THUMB_R
    pts = [p[j] for j in H.THUMB]
    tip_end = pts[3] - norm(pts[3] - pts[2]) * rr[3]
    out.append((pts[1], pts[2], rr[1], rr[2], H.THUMB[1]))
    out.append((pts[2], tip_end, rr[2], rr[3], H.THUMB[2]))
    out.append((pts[0], pts[1], rr[0], rr[1], H.THUMB[0]))
    for c in H.FINGERS:
        out.append((p[c[0]], p[c[1]], PALM_R, PALM_R, c[0]))
    return out


PALM_R = 0.0088  # half the palm's thickness, for the metacarpals' capsules


def seg_seg(a0, a1, b0, b1):
    """The closest points' parameters and distance between two segments."""
    d1, d2, r = a1 - a0, b1 - b0, a0 - b0
    a, e, f = d1 @ d1, d2 @ d2, d2 @ r
    c, b = d1 @ r, d1 @ d2
    den = a * e - b * b
    s = np.clip((b * f - c * e) / den, 0, 1) if den > 1e-12 else 0.0
    t = (b * s + f) / e
    if t < 0:
        t, s = 0.0, np.clip(-c / a, 0, 1)
    elif t > 1:
        t, s = 1.0, np.clip((b - c) / a, 0, 1)
    return s, t, np.linalg.norm(a0 + d1 * s - b0 - d2 * t)


def cap_gap(c1, c2):
    """The clearance between two rounded cones (negative: they pass through each other)."""
    s, t, d = seg_seg(c1[0], c1[1], c2[0], c2[1])
    return d - (c1[2] + (c1[3] - c1[2]) * s) - (c2[2] + (c2[3] - c2[2]) * t)


def cyl_gap(cap, centre, axis, radius, n=7, t0=0.0):
    """The clearance between a rounded cone (from `t0` of its length on) and an endless
    cylinder."""
    best = np.inf
    for i in range(n):
        t = t0 + (1 - t0) * i / (n - 1)
        q = cap[0] + (cap[1] - cap[0]) * t - centre
        d = np.linalg.norm(q - axis * (q @ axis))
        best = min(best, d - radius - (cap[2] + (cap[3] - cap[2]) * t))
    return best


def pose(fingers, thumb, cup=0):
    local = H.pose_local(fingers, thumb, cup)
    p, r = H.fk(local)
    return local, p, r


def pad_point(p, r, chain_tip, last, radius):
    """Where a finger tip's pad touches: under the tip, on the palm's side."""
    d = norm(p[chain_tip] - p[last])
    return p[chain_tip] - d * radius * 0.9 - r[last][:, 1] * radius * 0.85


def search(cost, x0, lo, hi, step=8.0, rounds=60):
    """A pattern search (no gradients): the best of x0 moved along each axis, the step halved
    when nothing is better."""
    x = np.clip(np.array(x0, float), lo, hi)
    best = cost(x)
    for _ in range(rounds):
        moved = False
        for i in range(len(x)):
            for s in (step, -step):
                y = x.copy()
                y[i] = np.clip(y[i] + s, lo[i], hi[i])
                c = cost(y)
                if c < best - 1e-12:
                    x, best, moved = y, c, True
                    break
        if not moved:
            step *= 0.5
            if step < 0.2:
                break
    return x, best


THUMB_LO = np.array([-20.0, -28.0, -25.0, -4.0, -4.0, -12.0])
THUMB_HI = np.array([55.0, 38.0, 45.0, 62.0, 78.0, 12.0])


def solve_thumb(fingers, target, start, cup=0, press=None, cylinder=None, rest=None, weight_rest=2e-7,
                gap=0.0004):
    """The thumb's angles that put its pad on `target` without passing through the fingers
    (or the handle); `press`: the way its pad should face."""
    rest = np.array(start if rest is None else rest, float)

    def cost(x):
        _, p, r = pose(fingers, tuple(x), cup)
        pad = pad_point(p, r, H.THUMB[3], H.THUMB[2], H.THUMB_R[3])
        c = ((pad - target) ** 2).sum()
        if press is not None:
            c += 2e-5 * (1 - (-r[H.THUMB[2]][:, 1]) @ press)
        caps = capsules(p, r)
        thumb_caps = caps[12:14]
        others = caps[:12] + caps[15:]
        for tc in thumb_caps:
            for oc in others:
                g = cap_gap(tc, oc)
                if g < gap:
                    c += (gap - g) ** 2 * 60
            if cylinder is not None:
                g = cyl_gap(tc, *cylinder)
                if g < gap:
                    c += (gap - g) ** 2 * 60
        return c + weight_rest * ((x - rest) ** 2).sum()

    best = None
    for s in (start, rest, (20, 0, 10, 20, 30, 0), (35, -10, 20, 30, 40, 0), (5, 20, 0, 5, 10, 0)):
        x, c = search(cost, s, THUMB_LO, THUMB_HI)
        if best is None or c < best[1]:
            best = (x, c)
    return tuple(np.round(best[0], 1)), best[1]


FINGER_LO = np.array([-12.0, -8.0, 0.0, 0.0])
FINGER_HI = np.array([14.0, 92.0, 108.0, 70.0])


def solve_finger(fi, fingers, thumb, target, start, cylinder=None, rest=None, weight_rest=1.5e-7):
    """One finger's angles that put its pad on `target` (clear of the handle)."""
    chain = H.FINGERS[fi]
    rest = np.array(start if rest is None else rest, float)

    def cost(x):
        f = list(fingers)
        f[fi] = tuple(x)
        _, p, r = pose(f, thumb)
        pad = pad_point(p, r, chain[4], chain[3], H.FINGER_R[chain][3])
        c = ((pad - target) ** 2).sum()
        if cylinder is not None:
            for cap in capsules(p, r)[fi * 3:fi * 3 + 3]:
                g = cyl_gap(cap, *cylinder)
                if g < 0:
                    c += g * g * 60
        # the last joint bends with the middle one, as a real finger's
        c += 4e-8 * (x[3] - 0.55 * x[2]) ** 2
        return c + weight_rest * ((x - rest) ** 2).sum()

    lo, hi = FINGER_LO.copy(), FINGER_HI.copy()
    x, c = search(cost, start, lo, hi)
    return tuple(np.round(x, 1)), c


def wrap_fingers(centre, axis, radius, spreads, which=(0, 1, 2, 3), squeeze=0.0003, limits=(92, 108, 70), cup=0,
                 back_off=False):
    """Closes fingers round a cylinder: each phalanx in turn bends until it touches."""
    out = []
    for fi in range(4):
        ang = [spreads[fi], 0.0, 0.0, 0.0]
        if fi in which:
            for k in range(3):
                a = -6.0 if k == 0 else 0.0
                while a < limits[k]:
                    ang[1 + k] = a
                    f = [tuple(ang) if i == fi else (spreads[i], 0, 0, 0) for i in range(4)]
                    _, p, r = pose(f, H.BIND_THUMB, cup)
                    if cyl_gap(capsules(p, r)[fi * 3 + k], centre, axis, radius, t0=0.0 if k == 0 else 0.4) <= -squeeze:
                        break
                    a += 0.5
            # and no bone left deeper in the handle than skin gives
            while back_off and ang[2] > 0:
                f = [tuple(ang) if i == fi else (spreads[i], 0, 0, 0) for i in range(4)]
                _, p, r = pose(f, H.BIND_THUMB, cup)
                if min(cyl_gap(c, centre, axis, radius, n=9) for c in capsules(p, r)[fi * 3:fi * 3 + 3]) >= -0.0010:
                    break
                ang[2] -= 0.5
        out.append(tuple(ang))
    return out


# ---- holding a handle ----
GRIP_TILT_DEG = 35.0   # the handle's lean in the hand: its thumb end toward the fingers
GRIP_X = 0.0780        # how far from the wrist the handle crosses the hand: under the knuckles


def hand_cylinder(radius, x0=GRIP_X, tilt_deg=GRIP_TILT_DEG):
    """A handle of `radius` lying in the palm: its centre and axis in the hand's space, as
    close to the palm as its metacarpals let it be."""
    import mesh as M
    t = np.radians(tilt_deg)
    axis = np.array([np.sin(t), 0.0, np.cos(t)])
    ang = np.linspace(0, 2 * np.pi, 36, endpoint=False)
    along = np.linspace(-0.034, 0.020, 12)
    side = np.cross([0.0, 1.0, 0.0], axis)
    ring = np.array([np.cos(a) * np.array([0.0, 1.0, 0.0]) + np.sin(a) * side for a in ang])
    pts = (ring[None] * radius + (along[:, None] * axis)[:, None]).reshape(-1, 3)
    lo, hi = -0.08, 0.0
    for _ in range(30):
        y = 0.5 * (lo + hi)
        if M.palm_field(pts + np.array([x0, y, 0.0])).min() < 0:
            hi = y
        else:
            lo = y
    return np.array([x0, lo, 0.0]), axis


def controller_fit(centre, axis, roll_deg=0.0, slide=0.0):
    """The left controller's aim space into the hand's (rotation, translation: hand = r @ aim
    + t), its handle on the hand's cylinder; `roll_deg` turns the hand round the handle,
    `slide` moves it along."""
    f1 = HANDLE_AXIS
    out = norm(np.array([-1.0, 0.0, 0.0]) - f1 * (f1 @ np.array([-1.0, 0.0, 0.0])))
    a = np.radians(roll_deg)
    f2 = out * np.cos(a) + np.cross(f1, out) * np.sin(a)   # from the handle toward the palm
    f3 = np.cross(f1, f2)
    e1 = axis
    e2 = np.array([0.0, 1.0, 0.0])
    e3 = np.cross(e1, e2)
    r = np.stack([e1, e2, e3], 1) @ np.stack([f1, f2, f3], 1).T
    t = centre - r @ (HANDLE_CENTRE + HANDLE_AXIS * slide)
    return r, t


def on_phalanx(fingers, thumb, joint, nxt, t=0.5, lift=0.0005, cup=0, radius=0.009):
    """A point on the back of a phalanx (and the way a pad pressing there faces)."""
    _, p, r = pose(fingers, thumb, cup)
    up = r[joint][:, 1]
    return p[joint] + (p[nxt] - p[joint]) * t + up * (radius + lift), -up


def curl_last(f, least=0.5, most=70.0):
    """The last joint bends with the middle one when nothing stops it."""
    return [(s, a, b, max(c, min(most, least * b))) for s, a, b, c in f]


HELD_CUP = 0.0
CUP = 6.0                 # the ring and little metacarpals fold in when the hand closes
HELD_ROLL_DEG = 0.0       # the hand round the handle (controller_fit)
HELD_SLIDE = 0.016        # and up it: the index finger level with the trigger
FIST_RADIUS = 0.0090
FIST_X = 0.0745           # where the fist's rod crosses the palm
FIST_TILT_DEG = 8.0


def keys(log=lambda *a: None):
    """The key poses, per group of joints: thumb, index, the other three fingers.
    Returns (thumb {name: angles}, index {name: angles}, rest {name: (three fingers, cup)}),
    the named poses {name: (thumb key, index key, rest key)}, and the held handle's cylinder
    and the controller's fit."""
    thumb, index, rest = {}, {}, {}
    # free: no controller drawn
    thumb['open'], index['open'], rest['open'] = OPEN[1], OPEN[0][0], (OPEN[0][1:], 0.0)
    thumb['relaxed'], index['relaxed'], rest['relaxed'] = RELAXED[1], RELAXED[0][0], (RELAXED[0][1:], 2.0)
    c, ax = hand_cylinder(FIST_RADIUS, FIST_X, FIST_TILT_DEG)
    fist = curl_last(wrap_fingers(c, ax, FIST_RADIUS, (-2, 0, 2, 5), limits=(88, 104, 70)), 0.62)
    log('fist', fist)
    index['fist'], rest['fist'] = fist[0], (fist[1:], CUP)
    index['point'] = POINT_INDEX
    tgt, press = on_phalanx(fist, H.BIND_THUMB, H.MIDDLE[2], H.MIDDLE[3], 0.30, cup=CUP,
                            radius=H.FINGER_R[H.MIDDLE][1])
    thumb['fist'], cst = solve_thumb(fist, tgt, (30, -8, 15, 30, 45, 0), CUP, press)
    log('thumb fist', thumb['fist'], cst)
    # thumbs up: the thumb straight out along the hand's thumb side
    _, p, r = pose(fist, H.BIND_THUMB, CUP)
    up_tgt = p[H.THUMB[0]] + norm([0.30, 0.10, 1.0]) * 0.093
    thumb['up'], cst = solve_thumb(fist, up_tgt, (-10, 30, 0, -5, -5, 0), CUP, norm([0.9, -0.3, -0.2]),
                                   weight_rest=0)
    log('thumb up', thumb['up'], cst)
    # held: round the controller's handle
    c, ax = hand_cylinder(HANDLE_RADIUS)
    cr, ct = controller_fit(c, ax, HELD_ROLL_DEG, HELD_SLIDE)
    cyl = (c, ax, HANDLE_RADIUS)
    to_hand = lambda q: cr @ q + ct
    held = wrap_fingers(c, ax, HANDLE_RADIUS, (-1, 0, 2, 4), cup=HELD_CUP, back_off=True)
    log('held', held)
    rest['held'] = (held[1:], HELD_CUP)
    rest['held_open'] = ([(s + d, a * 0.45, b * 0.5, cc * 0.5) for (s, a, b, cc), d in
                          zip(held[1:], (0, -2, -4))], 1.0)
    idle = H.BIND_THUMB
    index['held_touch'], cst = solve_finger(0, held, idle, to_hand(TRIGGER_FRONT), (0, 25, 45, 25), cyl)
    log('index touch', index['held_touch'], cst)
    index['held_pull'], cst = solve_finger(0, held, idle, to_hand(TRIGGER_FRONT + TRIGGER_TRAVEL),
                                           index['held_touch'], cyl)
    log('index pull', index['held_pull'], cst)
    s, a, b, cc = index['held_touch']
    index['held_off'] = (s + 3, a - 14, b - 20, cc - 8)
    f = [index['held_touch']] + held[1:]
    top = cr @ TOP_NORMAL
    for name, q, lift in (('held_rest', THUMB_REST, 0.0), ('held_stick', STICK_TOP, 0.0),
                          ('held_buttons', BUTTONS_TOP, 0.0), ('held_up', STICK_TOP, 0.022)):
        start = thumb.get('held_rest', (25, 5, 10, 15, 25, 0))
        thumb[name], cst = solve_thumb(f, to_hand(q) + top * lift, start, HELD_CUP, -top, cyl,
                                       rest=thumb.get('held_rest'))
        log('thumb', name, thumb[name], cst)
    named = {
        'open': ('open', 'open', 'open'), 'relaxed': ('relaxed', 'relaxed', 'relaxed'),
        'fist': ('fist', 'fist', 'fist'), 'point': ('fist', 'point', 'fist'),
        'thumb_up': ('up', 'fist', 'fist'),
        'grip': ('held_rest', 'held_touch', 'held'), 'trigger': ('held_rest', 'held_pull', 'held'),
        'grip_thumb_up': ('held_up', 'held_touch', 'held'), 'grip_stick': ('held_stick', 'held_touch', 'held'),
        'grip_buttons': ('held_buttons', 'held_touch', 'held'), 'grip_open': ('held_up', 'held_off', 'held_open'),
    }
    return thumb, index, rest, named, cyl, (cr, ct)


def named_angles(k, name):
    thumb, index, rest, named = k[:4]
    t, i, r = named[name]
    return [index[i]] + list(rest[r][0]), thumb[t], rest[r][1]
