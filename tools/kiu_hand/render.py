# Copyright 2026 AzaharFrame contributors
# Licensed under GPLv3 or any later version
# Refer to the license.txt file included.
"""A small software rasteriser for looking at the hand on the desktop (perspective, z-buffer,
smooth normals, vertex colours, one key light and a soft fill). Needs Pillow; only the preview
sheets use it (sheets.py), the generator does not."""
import numpy as np
from PIL import Image, ImageDraw


def look(eye, at, up=(0, 1, 0)):
    eye = np.asarray(eye, float)
    at = np.asarray(at, float)
    f = at - eye
    f /= np.linalg.norm(f)
    r = np.cross(f, up)
    if np.linalg.norm(r) < 1e-6:
        r = np.cross(f, (0, 0, 1))
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    return eye, r, u, f


def draw(meshes, cam, size=420, fov_deg=40.0, ss=2, bg=(38, 40, 46), flat=False):
    """meshes: [(V (n,3), N (n,3), C (n,3) 0..1 sRGB, T (m,3))]."""
    W = H = size * ss
    eye, r, u, f = cam
    img = np.zeros((H, W, 3))
    img[:] = np.array(bg) / 255.0
    zb = np.full((H, W), np.inf)
    key = norm3(-f * 0.55 + u * 0.75 + r * 0.35)
    fill = norm3(-f * 0.4 - u * 0.3 - r * 0.8)
    foc = (W / 2) / np.tan(np.radians(fov_deg) / 2)
    for V, N, C, T in meshes:
        q = V - eye
        z = q @ f
        zs = np.maximum(z, 1e-6)
        S = np.stack([W / 2 + (q @ r) * foc / zs, H / 2 - (q @ u) * foc / zs], -1)
        for t in T:
            zz = z[t]
            if (zz < 1e-3).any():
                continue
            a, b, c = S[t]
            d = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])
            if d >= -1e-9:  # back face (y runs down)
                continue
            x0, x1 = int(max(0, np.floor(min(a[0], b[0], c[0])))), int(min(W - 1, np.ceil(max(a[0], b[0], c[0]))))
            y0, y1 = int(max(0, np.floor(min(a[1], b[1], c[1])))), int(min(H - 1, np.ceil(max(a[1], b[1], c[1]))))
            if x0 > x1 or y0 > y1:
                continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            w1 = ((xs - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (ys - a[1])) / d
            w2 = ((b[0] - a[0]) * (ys - a[1]) - (xs - a[0]) * (b[1] - a[1])) / d
            w0 = 1 - w1 - w2
            m = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
            if not m.any():
                continue
            iz = w0 / zz[0] + w1 / zz[1] + w2 / zz[2]
            zp = (1 / iz)[m]
            yy, xx = np.nonzero(m)
            yy += y0
            xx += x0
            ok = zp < zb[yy, xx]
            if not ok.any():
                continue
            yy, xx, zp = yy[ok], xx[ok], zp[ok]
            bw = np.stack([w0[m][ok] / zz[0], w1[m][ok] / zz[1], w2[m][ok] / zz[2]], 1) * zp[:, None]
            if flat:
                n = np.cross(V[t[1]] - V[t[0]], V[t[2]] - V[t[0]])[None]
            else:
                n = bw @ N[t]
            n = n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
            col = (bw @ C[t]) ** 2.2
            lam = np.maximum(n @ key, 0.0)
            wrap = np.maximum(n @ fill, 0.0)
            rim = np.clip(1.0 + n @ f, 0, 1) ** 3
            light = 0.30 + 0.72 * lam + 0.16 * wrap + 0.10 * rim
            img[yy, xx] = col * light[:, None]
            zb[yy, xx] = zp
    img = np.clip(img, 0, 1) ** (1 / 2.2)
    img = img.reshape(size, ss, size, ss, 3).mean((1, 3))
    return Image.fromarray(np.clip(img * 255 + 0.5, 0, 255).astype(np.uint8))


def norm3(v):
    return v / np.linalg.norm(v)


def label(im, text):
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, im.width, 16], fill=(0, 0, 0))
    d.text((4, 2), text, fill=(235, 235, 235))
    return im


def grid(ims, cols):
    w, h = ims[0].size
    rows = (len(ims) + cols - 1) // cols
    out = Image.new('RGB', (cols * w + (cols - 1) * 4, rows * h + (rows - 1) * 4), (90, 90, 90))
    for i, im in enumerate(ims):
        out.paste(im, ((i % cols) * (w + 4), (i // cols) * (h + 4)))
    return out
