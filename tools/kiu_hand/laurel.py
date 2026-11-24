# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""The laurel frame (src/frame_xr/vis/laurel_frame.h): our own thin gold wreath round a 4:3
panel, and the leather plate it lies on. In the panel's space: the panel is 1 wide and 0.75
high, centred on the origin in the x-y plane, +y its top, +z its front. Two branches start
under the panel's middle, run out along its lower edge, rise round its sides and bend in over
its top, their tips not quite meeting; every leaf stays outside the panel."""
import numpy as np

GOLD, STRAP = 6, 7  # build.py's materials

HALF_W, HALF_H = 0.5, 0.375
MARGIN = 0.090          # the stem's distance from the panel's edge
CORNER = 0.135          # the stem's corner radius
LEAF_GAP = 0.012        # the least distance kept between any leaf and the panel
PLATE_OUT = 0.200       # the plate's edge beyond the panel's
PLATE_Z = (-0.040, -0.006)


def norm(v):
    return v / np.linalg.norm(v)


def stem_path(n=400):
    """The right branch's middle line from under the panel's middle to its tip over the top:
    points (n, 2) and unit tangents, evenly spaced."""
    hw, hh, r = HALF_W + MARGIN, HALF_H + MARGIN, CORNER
    pts = []
    # along the bottom, a little dip at the start where the two branches cross
    for t in np.linspace(0, 1, 60, endpoint=False):
        pts.append((0.02 + (hw - r - 0.02) * t, -hh - 0.012 * (1 - t) ** 2))
    for a in np.linspace(-90, 0, 40, endpoint=False):
        pts.append((hw - r + r * np.cos(np.radians(a)), -hh + r + r * np.sin(np.radians(a))))
    for t in np.linspace(0, 1, 60, endpoint=False):
        pts.append((hw, -hh + r + (2 * hh - 2 * r) * t))
    for a in np.linspace(0, 90, 40, endpoint=False):
        pts.append((hw - r + r * np.cos(np.radians(a)), hh - r + r * np.sin(np.radians(a))))
    for t in np.linspace(0, 1, 60):
        pts.append((hw - r - (hw - r - 0.075) * t, hh + 0.010 * t ** 2))
    pts = np.array(pts)
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    arc = np.concatenate([[0], np.cumsum(seg)])
    s = np.linspace(0, arc[-1], n)
    out = np.stack([np.interp(s, arc, pts[:, 0]), np.interp(s, arc, pts[:, 1])], 1)
    tan = np.gradient(out, axis=0)
    tan /= np.linalg.norm(tan, axis=1, keepdims=True)
    return out, tan, arc[-1]


def leaf(base, direction, length, width, lift, tilt):
    """A leaf: a closed pointed blade with a raised midrib, its tip lifted off the plate.
    Returns v (8, 3) and 12 triangles."""
    d = np.array([direction[0], direction[1], 0.0])
    side = np.array([-direction[1], direction[0], 0.0])
    b = np.array([base[0], base[1], 0.004])
    z = np.array([0.0, 0.0, 1.0])

    def at(t, w, h=0.0):
        return b + d * length * t + side * width * w + z * (tilt * length * t + h)

    v = np.array([at(0.0, 0.0), at(0.36, 0.50), at(0.70, 0.40), at(1.0, 0.0), at(0.70, -0.40), at(0.36, -0.50),
                  at(0.36, 0.0, lift), at(0.72, 0.0, lift * 0.8)])
    top = [(0, 6, 1), (1, 6, 7), (1, 7, 2), (2, 7, 3), (3, 7, 4), (4, 7, 6), (4, 6, 5), (5, 6, 0)]
    bottom = [(0, 1, 2), (0, 2, 3), (0, 3, 4), (0, 4, 5)]
    tris = [t if np.cross(v[t[1]] - v[t[0]], v[t[2]] - v[t[0]])[2] > 0 else t[::-1] for t in top]
    tris += [t if np.cross(v[t[1]] - v[t[0]], v[t[2]] - v[t[0]])[2] < 0 else t[::-1] for t in bottom]
    return v, np.array(tris)


def tube(points, radii, sides=3, phase=0.0):
    """A closed tube along 3D points (capped), `sides` round."""
    pts = np.asarray(points)
    v, tris = [], []
    for i, p in enumerate(pts):
        t = norm(pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)])
        up = np.array([0.0, 0.0, 1.0])
        a = norm(np.cross(up, t))
        b = np.cross(t, a)
        for k in range(sides):
            ang = phase + 2 * np.pi * k / sides
            v.append(p + (np.cos(ang) * a + np.sin(ang) * b) * radii[i])
    for i in range(len(pts) - 1):
        for k in range(sides):
            k1 = (k + 1) % sides
            a0, a1, b0, b1 = i * sides + k, i * sides + k1, (i + 1) * sides + k, (i + 1) * sides + k1
            tris += [(a0, a1, b1), (a0, b1, b0)]
    c0, c1 = len(v), len(v) + 1
    v += [pts[0], pts[-1]]
    last = (len(pts) - 1) * sides
    for k in range(sides):
        k1 = (k + 1) % sides
        tris += [(c0, k1, k), (c1, last + k, last + k1)]
    v, tris = np.array(v), np.array(tris)
    return v, orient(v, tris)


def orient(v, tris):
    """Winds a closed convex-ish shape's triangles outward (by its signed volume)."""
    c = v.mean(0)
    w = v - c
    vol = (np.cross(w[tris[:, 0]], w[tris[:, 1]]) * w[tris[:, 2]]).sum()
    return tris if vol > 0 else tris[:, ::-1]


def panel_clearance(v2):
    """How far a point is outside the panel (negative inside)."""
    dx = np.abs(v2[..., 0]) - HALF_W
    dy = np.abs(v2[..., 1]) - HALF_H
    return np.where((dx > 0) | (dy > 0), np.hypot(np.maximum(dx, 0), np.maximum(dy, 0)), np.maximum(dx, dy))


def branch(mirror=False):
    path, tan, total = stem_path()
    n = len(path)
    parts = []
    # the stem: thick at its foot, a thread at its tip
    idx = np.linspace(0, n - 1, 26).astype(int)
    pts3 = np.array([[path[i][0], path[i][1], 0.011] for i in idx])
    radii = np.array([0.0115 * (1 - 0.72 * (i / (n - 1)) ** 1.5) for i in idx])
    parts.append(tube(pts3, radii, 3, phase=np.pi / 2))
    # the leaves: pairs along the stem, the outer one bigger, all leaning toward the tip
    count = 17
    for k in range(count):
        f = (k + 0.55) / count
        i = int(f * (n - 1) * 0.985)
        size = 1.0 - 0.42 * f ** 1.6
        t, p = tan[i], path[i]
        nrm = np.array([t[1], -t[0]])  # outward (away from the panel) for this winding
        for side_sign, length, width, lean in ((1, 0.165, 0.066, 30), (-1, 0.135, 0.056, 22)):
            a = np.radians(lean) * side_sign
            d = t * np.cos(a) + nrm * np.sin(a)
            ln = length * size
            base = p + nrm * side_sign * 0.006
            # an inner leaf is shortened until it clears the panel
            for _ in range(12):
                v, tris = leaf(base, d, ln, width * size, 0.013 * size, 0.16)
                if panel_clearance(v[:, :2]).min() >= LEAF_GAP:
                    break
                ln *= 0.9
            parts.append((v, tris))
    # the tip: one last leaf along the stem
    v, tris = leaf(path[-8], tan[-8], 0.105, 0.046, 0.011, 0.2)
    parts.append((v, tris))
    if mirror:
        parts = [(v * np.array([-1.0, 1, 1]), t[:, ::-1]) for v, t in parts]
    return parts


def knot():
    """Where the two branches cross under the panel: a small band tied round them."""
    y = -(HALF_H + MARGIN) - 0.010
    pts = np.array([[-0.030, y, 0.014], [0.0, y, 0.016], [0.030, y, 0.014]])
    return tube(pts, np.array([0.019, 0.022, 0.019]), 6)


def rounded_rect(hw, hh, r, per_corner=6):
    pts = []
    for cx, cy, a0 in ((hw - r, hh - r, 0), (-hw + r, hh - r, 90), (-hw + r, -hh + r, 180), (hw - r, -hh + r, 270)):
        for a in np.linspace(a0, a0 + 90, per_corner + 1):
            pts.append((cx + r * np.cos(np.radians(a)), cy + r * np.sin(np.radians(a))))
    return np.array(pts)


def plate():
    """The leather plate under the panel and the wreath: a rounded slab with a bevelled edge."""
    hw, hh = HALF_W + PLATE_OUT, HALF_H + PLATE_OUT
    outer = rounded_rect(hw, hh, 0.19)
    inner = rounded_rect(hw - 0.016, hh - 0.016, 0.176)
    k = len(outer)
    z0, z1 = PLATE_Z
    v = [np.array([x, y, z0]) for x, y in outer] + [np.array([x, y, z1 - 0.010]) for x, y in outer] + \
        [np.array([x, y, z1]) for x, y in inner]
    c_bot, c_top = len(v), len(v) + 1
    v += [np.array([0, 0, z0]), np.array([0, 0, z1])]
    tris = []
    for i in range(k):
        j = (i + 1) % k
        tris += [(i, j, k + j), (i, k + j, k + i)]                  # the wall
        tris += [(k + i, k + j, 2 * k + j), (k + i, 2 * k + j, 2 * k + i)]  # the bevel
        tris += [(2 * k + i, 2 * k + j, c_top)]                      # the face
        tris += [(j, i, c_bot)]                                      # the back
    v, tris = np.array(v), np.array(tris)
    return v, orient(v, tris)


def build():
    """[(name, material, v, tris, shade)]: the wreath and the plate, each closed shape with
    its own vertices."""
    out = []
    leaves = branch(False) + branch(True) + [knot()]
    for v, t in leaves:
        z = v[:, 2]
        shade = np.clip(0.62 + 0.38 * (z - 0.004) / 0.03, 0.55, 1.0)
        out.append(('laurel', GOLD, v, t, shade))
    v, t = plate()
    out.append(('plate', STRAP, v, t, np.where(v[:, 2] > PLATE_Z[1] - 0.001, 1.0, 0.8)))
    return out
