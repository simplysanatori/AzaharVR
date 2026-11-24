# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""Pit's arm, drawn: pictures of it without the app. A small software rasteriser (numpy) and
two looks: clay (shape only) and the app's own fragment shader for the arm, ported line by line
from src/frame_xr/vis/pit_hand_shader.h (kPitHandFrag), so a picture here is what the app
draws. PNG writing with zlib from the standard library."""
import struct
import zlib

import numpy as np


def norm(v):
    v = np.asarray(v, float)
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-12)


def write_png(path, rgba):
    """An (H, W, 3 or 4) uint8 image as a PNG (filter 0, deflate 9: the same bytes each run)."""
    h, w, c = rgba.shape
    raw = np.zeros((h, w * c + 1), np.uint8)
    raw[:, 1:] = rgba.reshape(h, w * c)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6 if c == 4 else 2, 0, 0, 0))
    out += chunk(b'IDAT', zlib.compress(raw.tobytes(), 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(out)


def srgb_to_linear(c):
    c = np.asarray(c, float)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c < 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def look_at(eye, target, up=(0, 1, 0)):
    eye, target = np.asarray(eye, float), np.asarray(target, float)
    f = norm(target - eye)
    r = norm(np.cross(f, up))
    u = np.cross(r, f)
    return np.stack([r, u, -f]), eye   # rows: right, up, back


def raster(v, tris, eye, target, up=(0, 1, 0), fov=35.0, size=(800, 800), cull=True):
    """The nearest triangle and its barycentric weights at every pixel: (H, W) ints (-1: none),
    (H, W, 3) floats (perspective correct)."""
    w, h = size
    rot, eye = look_at(eye, target, up)
    c = (v - eye) @ rot.T
    z = -c[:, 2]
    f = 1.0 / np.tan(np.radians(fov) / 2)
    ok = z > 1e-4
    zs = np.where(ok, z, 1.0)
    sx = (c[:, 0] * f / zs * (h / w) * 0.5 + 0.5) * w
    sy = (0.5 - c[:, 1] * f / zs * 0.5) * h
    iz = 1.0 / zs
    tri_id = np.full((h, w), -1, np.int32)
    depth = np.zeros((h, w))
    bary = np.zeros((h, w, 3))
    t = tris
    x0, y0, x1, y1, x2, y2 = sx[t[:, 0]], sy[t[:, 0]], sx[t[:, 1]], sy[t[:, 1]], sx[t[:, 2]], sy[t[:, 2]]
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    vis = ok[t].all(1) & (np.abs(area) > 1e-9)
    if cull:
        vis &= area < 0   # counter-clockwise from outside, y down on screen
    lo_x = np.floor(np.minimum(np.minimum(x0, x1), x2)).astype(int)
    hi_x = np.ceil(np.maximum(np.maximum(x0, x1), x2)).astype(int)
    lo_y = np.floor(np.minimum(np.minimum(y0, y1), y2)).astype(int)
    hi_y = np.ceil(np.maximum(np.maximum(y0, y1), y2)).astype(int)
    vis &= (hi_x >= 0) & (lo_x < w) & (hi_y >= 0) & (lo_y < h)
    for i in np.nonzero(vis)[0]:
        ax, bx = max(lo_x[i], 0), min(hi_x[i], w - 1)
        ay, by = max(lo_y[i], 0), min(hi_y[i], h - 1)
        if bx < ax or by < ay:
            continue
        px = np.arange(ax, bx + 1) + 0.5
        py = (np.arange(ay, by + 1) + 0.5)[:, None]
        a = area[i]
        w0 = ((x1[i] - px) * (y2[i] - py) - (x2[i] - px) * (y1[i] - py)) / a
        w1 = ((x2[i] - px) * (y0[i] - py) - (x0[i] - px) * (y2[i] - py)) / a
        w2 = 1.0 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue
        i0, i1, i2 = iz[t[i, 0]], iz[t[i, 1]], iz[t[i, 2]]
        d = w0 * i0 + w1 * i1 + w2 * i2
        sub = depth[ay:by + 1, ax:bx + 1]
        m = inside & (d > sub)
        if not m.any():
            continue
        sub[m] = d[m]
        tri_id[ay:by + 1, ax:bx + 1][m] = i
        bb = np.stack([w0 * i0, w1 * i1, w2 * i2], -1) / d[..., None]
        bary[ay:by + 1, ax:bx + 1][m] = bb[m]
    return tri_id, bary


def interp(attr, tris, tri_id, bary):
    m = tri_id >= 0
    t = tris[tri_id[m]]
    b = bary[m]
    return attr[t[:, 0]] * b[:, :1] + attr[t[:, 1]] * b[:, 1:2] + attr[t[:, 2]] * b[:, 2:3]


def downsample(img, ss):
    h, w, c = img.shape
    return img.reshape(h // ss, ss, w // ss, ss, c).mean((1, 3))


def default_key(eye, target, up=(0, 1, 0)):
    """The key light: from the camera's side, raised and to the left."""
    rot, _ = look_at(eye, target, up)
    return norm(rot[2] * 0.75 + rot[1] * 0.75 - rot[0] * 0.45)


def clay(mesh, eye, target, up=(0, 1, 0), fov=35.0, size=(800, 800), ss=2, bg=(1.0, 1.0, 1.0), key=None):
    """Shape only: one grey, a key light and a soft fill."""
    v, n, tris = mesh['v'], mesh['n'], mesh['tris']
    w, h = size[0] * ss, size[1] * ss
    tri_id, bary = raster(v, tris, eye, target, up, fov, (w, h))
    m = tri_id >= 0
    img = np.ones((h, w, 3)) * np.array(bg)
    nn = norm(interp(n, tris, tri_id, bary))
    pos = interp(v, tris, tri_id, bary)
    L = default_key(eye, target, up) if key is None else norm(key)
    V = norm(np.asarray(eye, float) - pos)
    ndl = (nn * L).sum(1)
    ndv = np.clip((nn * V).sum(1), 0, 1)
    H = norm(L + V)
    lit = 0.30 + 0.62 * np.clip(ndl * 0.6 + 0.4, 0, 1) ** 1.6 + 0.10 * np.clip((nn * np.array(up)).sum(1) * 0.5 + 0.5, 0, 1)
    spec = 0.10 * np.clip((nn * H).sum(1), 0, 1) ** 30
    edge = 1.0 - 0.35 * (1 - ndv) ** 3
    base = np.array([0.80, 0.78, 0.75])
    if 'clay' in mesh:
        base = mesh['clay'][tris[tri_id[m]][:, 0]]
    img[m] = base * (lit * edge)[:, None] + spec[:, None]
    if mesh.get('wire'):
        img[m] *= (0.55 + 0.45 * (bary[m].min(1) > 0.05))[:, None]
    return (np.clip(downsample(img, ss), 0, 1) * 255 + 0.5).astype(np.uint8)


def sample(tex, uv):
    """Bilinear, clamped to the edge; `tex` (H, W, C) floats, uv with v down."""
    h, w = tex.shape[:2]
    x = np.clip(uv[:, 0] * w - 0.5, 0, w - 1)
    y = np.clip(uv[:, 1] * h - 0.5, 0, h - 1)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    x1, y1 = np.minimum(x0 + 1, w - 1), np.minimum(y0 + 1, h - 1)
    fx, fy = (x - x0)[:, None], (y - y0)[:, None]
    return (tex[y0, x0] * (1 - fx) + tex[y0, x1] * fx) * (1 - fy) + (tex[y1, x0] * (1 - fx) + tex[y1, x1] * fx) * fy


def mix(a, b, t):
    return a * (1 - t) + b * t


def sstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def pit_frag(pos, v_normal, v_tangent, uv, colour_lin, surface, eye, key, up, fade=1.0):
    """kPitHandFrag (pit_hand_shader.h) on arrays: returns premultiplied linear colour (N, 3)
    and alpha (N,). `colour_lin`: the colour image, rgb already linear, alpha the occlusion;
    `surface`: the surface image 0..1."""
    v3 = lambda *a: np.array(a, float)
    base = sample(colour_lin, uv)
    surf = sample(surface, uv)
    albedo = base[:, :3]
    ao = base[:, 3]

    def band(m, lo, hi):
        return sstep(lo - 0.03, lo + 0.03, m) * (1.0 - sstep(hi - 0.03, hi + 0.03, m))

    ng = norm(v_normal)
    t = v_tangent[:, :3] - ng * (ng * v_tangent[:, :3]).sum(1, keepdims=True)
    tl = (t * t).sum(1, keepdims=True)
    t = np.where(tl > 1e-10, t / np.sqrt(np.maximum(tl, 1e-20)), v3(1.0, 0.0, 0.0))
    b = np.cross(ng, t) * np.where(v_tangent[:, 3:4] < 0.0, -1.0, 1.0)
    bump = (surf[:, 0:2] - 128.0 / 255.0) * 2.0
    n = norm(t * bump[:, :1] + b * bump[:, 1:2] +
             ng * np.sqrt(np.maximum(1.0 - (bump * bump).sum(1, keepdims=True), 0.05)))
    V = norm(np.asarray(eye, float) - pos)
    L = norm(key)
    U = norm(up)
    ndl = (n * L).sum(1)
    ndv = np.maximum((n * V).sum(1), 0.0)
    sky = 0.5 + 0.5 * (n * U).sum(1)
    m = surf[:, 3]
    gold = (1.0 - sstep(0.17, 0.23, m))[:, None]
    enamel = band(m, 0.2, 0.4)[:, None]
    leather = band(m, 0.4, 0.6)[:, None]
    skin = band(m, 0.6, 0.8)[:, None]
    cloth = sstep(0.77, 0.83, m)[:, None]
    d = ndl * 0.85 + (sky - 0.5) * 0.3 - (1.0 - ao) * 0.35
    soft = (0.22 * skin + 0.30 * (cloth + leather) + 0.10 * (gold + enamel))[:, 0]
    lit = sstep(0.08 - soft, 0.08 + soft, d)[:, None]
    shade = (skin * v3(0.90, 0.63, 0.52) + cloth * v3(0.46, 0.50, 0.62) + leather * v3(0.50, 0.42, 0.42) +
             gold * v3(0.50, 0.43, 0.16) + enamel * v3(0.48, 0.30, 0.34))
    gain = (skin * v3(1.10, 1.02, 0.93) + cloth * v3(1.02, 1.02, 1.02) + leather * v3(1.06, 1.0, 0.96) +
            gold * v3(1.06, 1.0, 0.82) + enamel * v3(1.05, 1.05, 1.05))
    colour = albedo * mix(shade, gain, lit)
    edge = lit * (1.0 - lit) * 4.0
    colour = colour + skin * edge * albedo * v3(0.16, 0.02, -0.03)
    hidden = (1.0 - sstep(0.25, 0.95, ao))[:, None]
    colour = colour * mix(v3(1.0, 1.0, 1.0), skin * v3(0.90, 0.72, 0.64) + (1.0 - skin) * v3(0.62, 0.62, 0.62), hidden * 0.7)
    R = -V - 2.0 * (n * -V).sum(1, keepdims=True) * n
    above = 0.5 + 0.5 * (R * U).sum(1)
    glance = np.maximum((R * L).sum(1), 0.0)[:, None]
    metal = (albedo * mix(v3(0.42, 0.37, 0.13), v3(1.30, 1.18, 0.78), sstep(0.25, 0.80, above)[:, None]) +
             albedo * v3(0.55, 0.50, 0.30) * glance ** 6.0 + v3(1.0, 0.95, 0.75) * glance ** 90.0 * 0.9)
    colour = mix(colour, metal * mix(0.55, 1.0, sstep(0.2, 0.9, ao))[:, None], gold * 0.75)
    H = norm(L + V)
    ndh = np.maximum((n * H).sum(1), 0.0)[:, None]
    colour = colour + ((enamel * 0.45 * ndh ** 70.0 + leather * 0.07 * ndh ** 14.0 + skin * 0.05 * ndh ** 28.0) *
                       sstep(0.0, 0.3, ndl)[:, None])
    rim = ((1.0 - ndv) ** 4.0 * sstep(-0.2, 0.5, ndl))[:, None]
    colour = colour + rim * (skin * v3(0.30, 0.24, 0.18) + cloth * v3(0.10, 0.12, 0.16) + leather * v3(0.10, 0.07, 0.05) +
                             gold * v3(0.25, 0.22, 0.10))
    a = np.clip(fade, 0.0, 1.0) * np.clip(surf[:, 2] * 1.02, 0.0, 1.0)
    return np.clip(colour, 0.0, 1.0) * a[:, None], a


def painted(mesh, tex, eye, target, up=(0, 1, 0), fov=35.0, size=(800, 800), ss=2, bg=(1.0, 1.0, 1.0), key=None,
            light_up=(0, 1, 0), mirrored=False):
    """The arm as the app draws it. `mesh`: v, n, tan (N, 4), uv, tris; `tex`: (colour_lin,
    surface). `bg`: a colour (sRGB 0..1) or an (H*ss, W*ss, 3) linear image to draw over."""
    v, n, tris = mesh['v'], mesh['n'], mesh['tris']
    if mirrored:
        tris = tris[:, ::-1]
    w, h = size[0] * ss, size[1] * ss
    tri_id, bary = raster(v, tris, eye, target, up, fov, (w, h))
    m = tri_id >= 0
    if isinstance(bg, np.ndarray) and bg.ndim == 3:
        img = bg.copy()
    else:
        img = np.ones((h, w, 3)) * srgb_to_linear(np.array(bg))
    L = default_key(eye, target, up) if key is None else norm(key)
    col, a = pit_frag(interp(v, tris, tri_id, bary), interp(n, tris, tri_id, bary),
                      interp(mesh['tan'], tris, tri_id, bary), interp(mesh['uv'], tris, tri_id, bary),
                      tex[0], tex[1], eye, L, light_up)
    img[m] = col + img[m] * (1 - a[:, None])
    return img


def to_srgb8(img_lin, ss):
    return (linear_to_srgb(downsample(img_lin, ss)) * 255 + 0.5).astype(np.uint8)
