# Lane notes: `src/system/obj/ObjPtr_p.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `ObjPtrVec<T1, T2>::operator=()`

`ObjPtrVec<T1, T2>::operator=` -- moved verbatim from `src/system/obj/ObjPtr_p.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Attribution: ambiguous: 13 overloads of `ObjPtrVec<T1, T2>::operator=` fit equally (e.g. `??4?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@QAAXABV0@@Z`, `??4?$ObjPtrVec@VFlow@@VObjectDir@@@@QAAXABV0@@Z`, `??4?$ObjPtrVec@VRhythmDetector@@VObjectDir@@@@QAAXABV0@@Z`, `??4?$ObjPtrVec@VSpotlight@@VObjectDir@@@@QAAXABV0@@Z`); filed under the source name

Inside the body (was lines 312-329):

```text
        // Why (w12-o, 2026-10-02): this compiler does intra-TU interprocedural
        // escape analysis. It knows push_back(const Node &) only READS its
        // argument, so as long as newNode never escapes, neither push_back nor
        // Set can modify it, the ctor's mObject = 0 reaches the destructor, and
        // ~ObjRefConcrete's `if (mObject) Release` folds away -- leaving only
        // the base-vptr reset, which is exactly the image. But the escape
        // analysis evidently runs before the ctor's dead `if (mObject) AddRef(this)`
        // branch is folded, and AddRef stores `this` into the referent's ring,
        // so ObjRefConcrete(nullptr) marks newNode escaped and the check (plus
        // the ring unlink, rows 69-77, 87.83784%) comes back. Scratch-TU proof
        // of the IPA: a callee that only reads `const S &` lets a later
        // `if (s.a)` fold; a callee that passes &s to an extern does not.
        //
        // Measured negatives under the old ctor (all 13 instantiations):
        //   `const Node newNode(this);`      byte-inert, 87.83784
        //   `mNodes.push_back(Node(this));`  85.09460
        //   explicit `mObject = 0` after Set 98.64865 (adds a store the image
        //       lacks; it was the clue that the image knew mObject was null)
```


## `??2@YAPAXI@Z`

`ObjPtrList<T1, T2>::Node::operator new` -- moved verbatim from `src/system/obj/ObjPtr_p.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Inside the body (was lines 780-787):

```text
// -- ObjPtrVec Xbox template implementations --
// w24-pch (B4, open): whether erase<T> inlines Set<T> follows COMDAT compile
// order per TU (w21-d rule; image inlines for Font RndMat, LightPreset
// Spotlight, CharClipGroup; calls for FlowManager FlowNode and LightPreset
// E/L/SD).  Defining erase BEFORE Set in this header is inert on all of them
// (per-object probe: erase<Spotlight> 83.12, erase<FlowNode> 80.58, control
// erase<RndMat> 100, unchanged).  Definition order is not the input; the open
// question is what reference makes the front end queue Set<T> ahead of erase<T>.
```
