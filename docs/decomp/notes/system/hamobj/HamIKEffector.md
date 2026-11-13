# Lane notes: `src/system/hamobj/HamIKEffector.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@HamIKEffector@@UAAXXZ`

`HamIKEffector::Poll` -- moved verbatim from `src/system/hamobj/HamIKEffector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 889-907):

```text
                    // w20-l: normalized 100 / fuzzy 99.94. Only rows: y/z `fmuls`
                    // operand order (image f12*f0 / f11*f0, ours f0*f12 / f0*f11) --
                    // commutative, value-identical. `q.v *= invWeight`, Scale(), and
                    // `q.v /= totalWeight` all emit the same code. Values and branch
                    // targets of the whole function checked against the image.
                    // Open (w25-gv, c2 §8.7 commutative key): the image emits
                    // (inv, x) but (y, inv) and (z, inv).  Here the scalar-replaced
                    // q.v components are variable leaves x=V15 > y=V13 > z=V11 (sid
                    // falls with first-def order) and invWeight is V282, so it leads
                    // all three.  The image needs sid(x) < sid(inv) < sid(y), sid(z),
                    // or y/z as memory leaves keyed above inv.  ~40 spellings tried
                    // (z,y,x / Zero() / Set() / x=y=z inits, refs, pointers, /=, inline
                    // reciprocal, decl placement): each either keeps x highest or
                    // reorders the init stores.
                    // w26-f14: re-measured inert (exact, per-object + patch chain) --
                    // `const float &qy = q.v.y` / `&qz` / `&qx` scalar bindings
                    // singly and paired, `Vector3 &qv = q.v` with the multiplies
                    // through qv, and a const or step-divided invWeight: all
                    // reproduce 99.93730 / 100.0 exactly.
```


## `?ComputeHandPullAndQuat@HamIKEffector@@IAAXAAVQuatXfm@@AAVTransform@@ABV3@ABVVector3@@@Z`

`HamIKEffector::ComputeHandPullAndQuat` -- moved verbatim from `src/system/hamobj/HamIKEffector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 1088-1109):

```text
// w21-am (98.31 -> 100.0, fuzzy 100): the rotation matrix is ONE
//   `xfmOut.m.Set(c, s, 0, -s, c, 0, 0, 0, 1)` -- the same call IKElbow makes
//   for its elbow matrix -- not nine member stores.  Through Set() MSVC
//   issues m.y.x right after the fneg and reloads mEffector after the m.y.y
//   store, which is the image's tail.  The separate early `m.x.z = 0` store
//   and the `effLocalV` binding are gone (Set writes m.x.z; the Multiply
//   reads mEffector->LocalXfm().v directly).  Same nine values.
// w21-b: 98.3 canonical (was 94.14), 6 rows left, all in the xfmOut tail:
//   the mEffector reload (`lwz r11, 0x50(r29)`) is scheduled one slot early
//   (before the m.y.y store at 0x14 instead of after it), and MSVC sinks our
//   `xfmOut.m.y.x = -sinAngle` store (0x10) below the three z-row stores
//   where the image issues it right after the fneg.  Same instructions, same
//   values.  The head rows the w7-y note below described (mEffector load
//   order, dz register pair, distSq chain) are closed by building the pull
//   in quatOut.v -- see the comment in the body.  Tail spellings measured
//   with a standalone /FAs probe and all inert or worse: the effLocalV
//   binding at each of the 10 statement positions or inline in the Multiply
//   call, m.y.x as -sinAngle / -m.x.y / a positive sqrt local, and all 720
//   orders of {binding, m.y.y, m.y.z, m.y.x, z-row} x z-row component order.
//
// Earlier history (w7-y, at 94.2): reassociating distSq and moving the
// effLocalV binding below the m.y.y store were both refuted there.
```

Inside the body (was lines 1113-1118):

```text
    // w21-b: the pull is built IN quatOut.v (as in ComputeElbowPullAndQuat)
    // and both lengths are read through mEffector directly; the squared
    // distance is accumulated y, z, x.  This gives the image's load order
    // (mEffector after the first lfs), the dz register pair, and its
    // dz*dz / fmadds dy / fmadds dx chain with dx parked in f11.  Values are
    // unchanged: (dy^2 + dz^2) + dx^2 is the image's own association.
```


## `?ComputeElbowPullAndQuat@HamIKEffector@@IAAXAAVQuatXfm@@ABVTransform@@ABVVector3@@@Z`

`HamIKEffector::ComputeElbowPullAndQuat` -- moved verbatim from `src/system/hamobj/HamIKEffector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 1306-1313):

```text
    // w21-b: 96.43 -> closed.  The w7-bb/w14-b residual (the image RELOADS
    // q.v.x after storing it, and squares in dy, x, dz order) is the pull
    // being built IN q.v: Subtract() writes all three components, the length
    // is summed from q.v, and q.v is scaled in place.  MSVC forwards y and z
    // (and drops their first stores as dead) but reloads x.  effectorV.x is
    // read before the Subtract: the image loads it (824BF900, lfs f10,0x0(r30))
    // ahead of the q.v.x store.  Found with a standalone /FAs probe over 364
    // spellings; same values as before on every component.
```
