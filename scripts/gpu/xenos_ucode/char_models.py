#!/usr/bin/env python3
"""Readable models of DC3's shipped per-pixel standard.ps permutations for
skin (shader_variation 1), hair (2) and the plain material with a rim, each
checked against an emulation of the shipped microcode (ucode_emu.py).

  python3 char_models.py [--ucode DIR] [--shaders DIR] [-n 2000]
      -> worst relative error, model vs microcode, per permutation

--ucode holds `<key>.ps.txt`, xenia-gpu-shader-compiler's ucode listing of
each permutation (dis.sh writes them); --shaders holds the cached blobs
`standard_<key>.ps`, whose leading literal table (c252.. or c248..c255) is
read from the blob. Neither is redistributable game data: the lane archive
keeps them under archive/np-gfx-2026-11-14/lanes/light/.

Interpolators: r0 uv, r1 world position, r2 V = camera - position (not
normalised), r3 normal, r4 tangent, r5 bitangent. Constants: c0 material
colour, c1 ambient, c2 specular (rgb, power), c14.x = 1 - de_normal, c19
second specular colour, c61 rim (rgb, power), c62..c64 point positions
(w falloff slope), c66..c68 point colours (w falloff offset), c80..c85 the box
map faces +X -X +Y -Y +Z -Z. Textures: tf0 diffuse, tf1 normal map, tf2
specular map.
"""
import argparse
import os
import struct

import numpy as np

from ucode_emu import Ucode

HERE = os.path.dirname(os.path.abspath(__file__))
ARCHIVE = "/home/free/code/milohax/dc3-decomp/archive/np-gfx-2026-11-14/lanes/light"
DEFAULT_UCODE = ARCHIVE + "/round2/ucode"
DEFAULT_SHADERS = ARCHIVE + "/shaders/standard"

SKIN = "0000032040035037"          # skin, rim, rim-under, normal + spec maps
SKIN_NO_RIM = "0000030040031037"   # skin, normal + spec maps, no rim
HAIR = "0000030080031017"          # hair, spec map
PLAIN_RIM = "0000032000031037"     # no variation, rim, normal + spec maps
PLAIN = "0000030000031017"         # no variation, spec map

LIT_BYTES = {SKIN_NO_RIM: 128}     # the others carry 64 bytes (c252..c255)


def literals(shaders_dir, key):
    n = LIT_BYTES.get(key, 64)
    with open(f"{shaders_dir}/standard_{key}.ps", "rb") as f:
        b = f.read(n)
    return list(struct.unpack(">%df" % (n // 4), b))


def sat(x):
    return np.clip(x, 0.0, 1.0)


def nrm(v):
    return v / np.linalg.norm(v)


def box(n, c):
    return (sat(n[0]) * c[80][:3] + sat(-n[0]) * c[81][:3] + sat(n[1]) * c[82][:3] +
            sat(-n[1]) * c[83][:3] + sat(n[2]) * c[84][:3] + sat(-n[2]) * c[85][:3])


def box_pow(r, c, p):
    def f(x):
        return sat(x) ** p
    return (f(r[0]) * c[80][:3] + f(-r[0]) * c[81][:3] + f(r[1]) * c[82][:3] +
            f(-r[1]) * c[83][:3] + f(r[2]) * c[84][:3] + f(-r[2]) * c[85][:3])


def lights(P, c):
    out = []
    for i in range(3):
        pos, col = c[62 + i], c[66 + i]
        d = pos[:3] - P
        dist = np.linalg.norm(d)
        att = sat(dist * pos[3] + col[3])
        out.append((d / dist, col[:3], att))
    return out


def bumped(n, t, b, nm, k):
    """(the normal with de_normal applied, the normal at full strength)"""
    x, y = nm[0] * 2 - 1, nm[1] * 2 - 1
    z = sat(1 - x * x - y * y)
    return nrm(z * n + k * x * b + k * y * t), nrm(z * n + x * b + y * t)


INV2PI = 0.15915494
INVPI = 0.31830987


def skin(I, c, tex, has_rim=True):
    P, V = I[1][:3], nrm(I[2][:3])
    n, t, b = I[3][:3], I[4][:3], I[5][:3]
    diff, nm, spt = tex[0], tex[1], tex[2]
    Nk, Nf = bumped(n, t, b, nm, c[14][0])
    sp = spt * c[2]
    power = max(sp[3], 0.5)
    rim_p = c[61][3]
    vnf = sat(V @ Nf)
    rim = ((1 - sat(V @ Nk)) * (0.5 * Nk[2] + 0.5)) ** rim_p if has_rim else 0.0
    sheen = (1 - vnf * vnf) ** rim_p if has_rim else 0.0
    light = (1 + rim * box(-V, c)) * box(Nk, c)
    back = np.zeros(3)
    spec = np.zeros(3)
    norm = (power + 2) * INV2PI
    fres = 4 * (1 - vnf * vnf) ** 0.7
    for L, col, att in lights(P, c):
        light = light + att * sat((Nk @ L) * np.array([0.55, 0.6, 0.65]) +
                                  np.array([0.45, 0.4, 0.35])) * col
        back = back + att * sat(-(L @ V)) * sheen * col
        R = 2 * (Nf @ L) * Nf - L
        spec = spec + att * col * fres * norm * sat(V @ R) ** power
    RE = 2 * (V @ Nf) * Nf - V
    rimf = (1 - vnf) * (0.5 * Nf[2] + 0.5)
    spec = spec + rimf * rimf * norm * box_pow(RE, c, power)
    rim_col = c[61][:3] if has_rim else np.zeros(3)
    col = (light * c[0][:3] + c[1][:3] * c[0][:3]) * diff[:3] * (1 + rim_col * back)
    return col + sp[:3] * spec


def skin_no_rim(I, c, tex):
    return skin(I, c, tex, has_rim=False)


def hair(I, c, tex):
    P, V = I[1][:3], nrm(I[2][:3])
    n_raw = I[3][:3]
    N = nrm(n_raw)
    diff, spt = tex[0], tex[2]
    power = max(spt[3] * c[2][3], 0.5)
    norm = (power + 2) * INV2PI
    light = box(N, c)
    RE = 2 * (V @ N) * N - V
    x = n_raw[0] * RE[1] - n_raw[1] * RE[0]          # RE . (z x n)
    s = abs(1 - x * x)
    lobe1 = s ** (4 * power)
    aniso = c[2][:3] * lobe1 + c[19][:3] * s ** (2 * power) * (1 - lobe1)
    rimb = (1 - sat(V @ N)) * (0.5 * N[2] + 0.5)
    spec = (0.25 + rimb) * norm * box_pow(RE, c, power)
    for L, col, att in lights(P, c):
        light = light + att * sat((N @ L) * np.array([0.75, 0.8, 0.85]) +
                                  np.array([0.25, 0.2, 0.15])) * col
        spec = spec + att * col * sat(L @ RE) * norm * aniso
    return (light * c[0][:3] + c[1][:3] * c[0][:3]) * diff[:3] + spec * spt[:3]


def plain(I, c, tex, has_rim=True, has_normal_map=True):
    P, V = I[1][:3], nrm(I[2][:3])
    n, t, b = I[3][:3], I[4][:3], I[5][:3]
    diff, nm, spt = tex[0], tex[1], tex[2]
    N = bumped(n, t, b, nm, c[14][0])[0] if has_normal_map else nrm(n)
    base = diff[:3] * c[0][:3]
    light = c[1][:3] + box(N, c)
    sp = spt * c[2]
    p = max(sp[3], 0.5)
    lobe = p * INV2PI + INVPI
    NV = N @ V
    R = 2 * NV * N - V
    spec = lobe * (0.25 + (1 - sat(NV)) * (0.5 * N[2] + 0.5)) * box_pow(R, c, p)
    for L, col, att in lights(P, c):
        light = light + att * col * max(N @ L, 0)
        spec = spec + lobe * att * col * sat(R @ L) ** p
    d = base * light
    if not has_rim:
        return d + sp[:3] * spec
    rp = c[61][3]
    X = ((1 - sat(NV)) * (0.5 * N[2] + 0.5)) ** rp * box(-V, c)
    sheen = (1 - sat(NV) ** 2) ** rp * (0.5 * N[2] + 0.5)
    for L, col, att in lights(P, c):
        X = X + att * col * sat(-(L @ V)) * sheen
    rim = c[61][:3] * X
    return d * (1 + 0.3 * rim) + 0.7 * rim + sp[:3] * spec


def plain_no_rim(I, c, tex):
    return plain(I, c, tex, has_rim=False, has_normal_map=False)


MODELS = {SKIN: skin, SKIN_NO_RIM: skin_no_rim, HAIR: hair, PLAIN_RIM: plain,
          PLAIN: plain_no_rim}


def rnd_inputs(rng, normal_scale=(1.0, 1.0)):
    I = {}
    I[0] = np.array([rng.random(), rng.random(), 0, 0])
    I[1] = np.append(rng.normal(size=3) * 50, 0)
    n = nrm(rng.normal(size=3))
    t = nrm(np.cross(n, rng.normal(size=3)))
    b = np.cross(n, t)
    I[2] = np.append(rng.normal(size=3) * 100, 0)
    n = n * rng.uniform(*normal_scale)   # the interpolated normal is not unit
    I[3], I[4], I[5] = (np.append(v, 0) for v in (n, t, b))
    c = {k: rng.random(4) for k in (0, 1, 2, 14, 19, 61, 80, 81, 82, 83, 84, 85)}
    c[2][3] = rng.uniform(5, 60)
    c[61][3] = rng.uniform(1, 8)
    for i in range(3):
        c[62 + i] = np.append(rng.normal(size=3) * 60, -rng.uniform(0.001, 0.02))
        c[66 + i] = np.append(rng.random(3), rng.uniform(0.5, 1.5))
    tex = {k: rng.random(4) for k in range(3)}
    tex[1][:2] = 0.5 + 0.3 * (tex[1][:2] - 0.5)
    tex[2][3] = rng.uniform(0.2, 1.0)
    return I, c, tex


def emulator(ucode_dir, shaders_dir, key):
    return Ucode(f"{ucode_dir}/{key}.ps.txt", literals(shaders_dir, key))


def check(ucode_dir, shaders_dir, key, n=2000, seed=1, normal_scale=(1.0, 1.0)):
    emu = emulator(ucode_dir, shaders_dir, key)
    rng = np.random.default_rng(seed)
    worst = 0.0
    for _ in range(n):
        I, c, tex = rnd_inputs(rng, normal_scale)
        got = emu.run(I, c, tex)["oC0"][:3]
        want = MODELS[key](I, c, tex)
        worst = max(worst, float(np.max(np.abs(got - want) / (1e-3 + np.abs(got)))))
    return worst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ucode", default=DEFAULT_UCODE)
    ap.add_argument("--shaders", default=DEFAULT_SHADERS)
    ap.add_argument("-n", type=int, default=2000)
    a = ap.parse_args()
    for key, model in MODELS.items():
        w1 = check(a.ucode, a.shaders, key, a.n)
        w2 = check(a.ucode, a.shaders, key, a.n // 4, seed=2, normal_scale=(0.8, 1.2))
        print(f"{key} {model.__name__:13s} worst rel err {w1:.2e} (unit normals), "
              f"{w2:.2e} (normal length 0.8..1.2)")


if __name__ == "__main__":
    main()
