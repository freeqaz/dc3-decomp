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
map faces +X -X +Y -Y +Z -Z, c106 normal detail (strength, tiling, 1, 1).
Textures: tf0 diffuse, tf1 normal map, tf2 specular map, tf14 the normal
detail map (fetched at uv * c106.y).
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
# Listings disassembled after round 2 (dis.sh), looked up here when --ucode
# does not hold them: item 13's normal detail, item 16's self-shadow (bit 19).
EXTRA_UCODE_DIRS = [ARCHIVE + "/../sharp/item13/c/ucode", ARCHIVE + "/../sharp/item16/ucode"]

SKIN = "0000032040035037"          # skin, rim, rim-under, normal + spec maps
SKIN_NO_RIM = "0000030040031037"   # skin, normal + spec maps, no rim
HAIR = "0000030080031017"          # hair, spec map
PLAIN_RIM = "0000032000031037"     # no variation, rim, normal + spec maps
PLAIN = "0000030000031017"         # no variation, spec map
PLAIN_DETAIL = "0000030001030035"  # no variation, normal map + normal detail (bit 24), specular

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


def shadow_mult(sh, N):
    """Self-shadow factor on the point lights (option bit 19, the character's
    shadow map). sh = (s, colour, camY): s is the lit fraction of the 2x2
    point-sampled depth compares, bilinearly weighted; colour is c107 (one
    minus the floor spot's colour, else white); camY is c108, the shadow
    camera's view axis. 1 without a shadow map."""
    if sh is None:
        return 1.0
    s, col, cam_y = sh
    return 1 - 0.75 * col[:3] * sat(N @ -cam_y[:3]) * (1 - s)


def bumped(n, t, b, nm, k, detail=None, strength=0.0):
    """(the normal with de_normal applied, the normal at full strength).
    With a normal detail texel (bit 24) its x, y and z = sat(1 - x^2 - y^2),
    times the strength c106.x, are added to the normal map's before the
    de_normal factor and the tangent frame."""
    x, y = nm[0] * 2 - 1, nm[1] * 2 - 1
    z = sat(1 - x * x - y * y)
    if detail is not None:
        dx, dy = detail[0] * 2 - 1, detail[1] * 2 - 1
        x, y = x + strength * dx, y + strength * dy
        z = z + strength * sat(1 - dx * dx - dy * dy)
    return nrm(z * n + k * x * b + k * y * t), nrm(z * n + x * b + y * t)


INV2PI = 0.15915494
INVPI = 0.31830987


def skin(I, c, tex, has_rim=True, sh=None):
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
    mk, mf = shadow_mult(sh, Nk), shadow_mult(sh, Nf)
    for L, col, att in lights(P, c):
        light = light + mk * att * sat((Nk @ L) * np.array([0.55, 0.6, 0.65]) +
                                       np.array([0.45, 0.4, 0.35])) * col
        back = back + mf * att * sat(-(L @ V)) * sheen * col
        R = 2 * (Nf @ L) * Nf - L
        spec = spec + mf * att * col * fres * norm * sat(V @ R) ** power
    RE = 2 * (V @ Nf) * Nf - V
    rimf = (1 - vnf) * (0.5 * Nf[2] + 0.5)
    spec = spec + rimf * rimf * norm * box_pow(RE, c, power)
    rim_col = c[61][:3] if has_rim else np.zeros(3)
    col = (light * c[0][:3] + c[1][:3] * c[0][:3]) * diff[:3] * (1 + rim_col * back)
    return col + sp[:3] * spec


def skin_no_rim(I, c, tex, sh=None):
    return skin(I, c, tex, has_rim=False, sh=sh)


def hair(I, c, tex, sh=None):
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
    m = shadow_mult(sh, N)
    for L, col, att in lights(P, c):
        light = light + m * att * sat((N @ L) * np.array([0.75, 0.8, 0.85]) +
                                      np.array([0.25, 0.2, 0.15])) * col
        spec = spec + m * att * col * sat(L @ RE) * norm * aniso
    return (light * c[0][:3] + c[1][:3] * c[0][:3]) * diff[:3] + spec * spt[:3]


def plain(I, c, tex, has_rim=True, has_normal_map=True, has_detail=False,
          has_spec_map=True, sh=None):
    P, V = I[1][:3], nrm(I[2][:3])
    n, t, b = I[3][:3], I[4][:3], I[5][:3]
    diff, nm = tex[0], tex[1]
    spt = tex[2] if has_spec_map else np.ones(4)
    detail = tex[14] if has_detail else None
    strength = c[106][0] if has_detail else 0.0
    N = bumped(n, t, b, nm, c[14][0], detail, strength)[0] if has_normal_map else nrm(n)
    base = diff[:3] * c[0][:3]
    light = c[1][:3] + box(N, c)
    sp = spt * c[2]
    p = max(sp[3], 0.5)
    lobe = p * INV2PI + INVPI
    NV = N @ V
    R = 2 * NV * N - V
    spec = lobe * (0.25 + (1 - sat(NV)) * (0.5 * N[2] + 0.5)) * box_pow(R, c, p)
    m = shadow_mult(sh, N)
    for L, col, att in lights(P, c):
        light = light + m * att * col * max(N @ L, 0)
        spec = spec + m * lobe * att * col * sat(R @ L) ** p
    d = base * light
    if not has_rim:
        return d + sp[:3] * spec
    rp = c[61][3]
    X = ((1 - sat(NV)) * (0.5 * N[2] + 0.5)) ** rp * box(-V, c)
    sheen = (1 - sat(NV) ** 2) ** rp * (0.5 * N[2] + 0.5)
    for L, col, att in lights(P, c):
        X = X + m * att * col * sat(-(L @ V)) * sheen
    rim = c[61][:3] * X
    return d * (1 + 0.3 * rim) + 0.7 * rim + sp[:3] * spec


def plain_no_rim(I, c, tex, sh=None):
    return plain(I, c, tex, has_rim=False, has_normal_map=False, sh=sh)


def plain_detail(I, c, tex):
    return plain(I, c, tex, has_rim=False, has_detail=True, has_spec_map=False)


MODELS = {SKIN: skin, SKIN_NO_RIM: skin_no_rim, HAIR: hair, PLAIN_RIM: plain,
          PLAIN: plain_no_rim, PLAIN_DETAIL: plain_detail}

# The same permutations with option bit 19 (the character self-shadow map,
# RndShadowMap / Character::mSelfShadow): an extra interpolator r6 (the
# position in the shadow camera's clip space), tf5 (the 512x512 depth map,
# four point fetches at +-0.5 texel and getWeights2D), c107 and c108. The
# interpolator is r6 after the tangent frame, r4 without a normal map.
SHADOWED = {"00000320400b5037": SKIN, "00000300400b1037": SKIN_NO_RIM,
            "00000300800b1017": HAIR, "00000320000b1037": PLAIN_RIM,
            "00000300000b1017": PLAIN}
SHADOW_REG = {"00000300800b1017": 4, "00000300000b1017": 4}


def rnd_shadow(rng, I, c, tex, reg=6, margin=0.0):
    """Shadow inputs from their own generator. Returns sh for the model and
    sets the emulator's: tf5 gives depth 9 (lit) or -9 (in shadow) per fetch,
    against r<reg> = (0.3, 0.4, 0.5, 1) (depth 0.5). The bilinear fractions
    are drawn from [margin, 1 - margin] (the GPU test keeps them off the texel
    edges, where float rounding could pick the neighbouring footprint)."""
    c[107] = rng.random(4)
    c[108] = np.append(nrm(rng.normal(size=3)), 1.0)
    I[reg] = np.array([0.3, 0.4, 0.5, 1.0])
    fx, fy = (margin + (1 - 2 * margin) * rng.random() for _ in range(2))
    tex[("w", 5)] = (fx, fy)
    lit = rng.random(4) > 0.5
    tex[5] = [np.full(4, 9.0 if x else -9.0) for x in lit]
    tex[("lit", 5)] = lit
    # fetch order: (-0.5, -0.5), (+0.5, -0.5), (-0.5, +0.5), (+0.5, +0.5)
    s = ((1 - fx) * (1 - fy) * lit[0] + fx * (1 - fy) * lit[1] +
         (1 - fx) * fy * lit[2] + fx * fy * lit[3])
    return (s, c[107], c[108])


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


def rnd_detail(rng, c, tex):
    """The normal detail inputs (c106, tf14), drawn from their own generator so
    that adding them leaves every other permutation's inputs as they were."""
    c[106] = np.array([rng.uniform(0.05, 1.5), rng.uniform(1, 60), 1.0, 1.0])
    tex[14] = rng.random(4)
    tex[14][:2] = 0.5 + 0.6 * (tex[14][:2] - 0.5)


def emulator(ucode_dir, shaders_dir, key):
    path = f"{ucode_dir}/{key}.ps.txt"
    for extra in EXTRA_UCODE_DIRS:
        if os.path.exists(path):
            break
        path = f"{extra}/{key}.ps.txt"
    return Ucode(path, literals(shaders_dir, key))


def check(ucode_dir, shaders_dir, key, n=2000, seed=1, normal_scale=(1.0, 1.0)):
    emu = emulator(ucode_dir, shaders_dir, key)
    rng = np.random.default_rng(seed)
    rng_detail = np.random.default_rng(seed + 1000)
    rng_shadow = np.random.default_rng(seed + 2000)
    worst = 0.0
    for _ in range(n):
        I, c, tex = rnd_inputs(rng, normal_scale)
        rnd_detail(rng_detail, c, tex)
        if key in SHADOWED:
            sh = rnd_shadow(rng_shadow, I, c, tex, SHADOW_REG.get(key, 6))
            got = emu.run(I, c, tex)["oC0"][:3]
            want = MODELS[SHADOWED[key]](I, c, tex, sh=sh)
        else:
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
    for key in list(MODELS) + list(SHADOWED):
        model = MODELS[SHADOWED.get(key, key)]
        w1 = check(a.ucode, a.shaders, key, a.n)
        w2 = check(a.ucode, a.shaders, key, a.n // 4, seed=2, normal_scale=(0.8, 1.2))
        print(f"{key} {model.__name__:13s} worst rel err {w1:.2e} (unit normals), "
              f"{w2:.2e} (normal length 0.8..1.2)")


if __name__ == "__main__":
    main()
