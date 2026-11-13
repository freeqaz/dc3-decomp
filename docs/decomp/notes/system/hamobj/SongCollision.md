# Lane notes: `src/system/hamobj/SongCollision.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Update@SongCollision@@QAAXPAVMoveDir@@@Z`

`SongCollision::Update` -- moved verbatim from `src/system/hamobj/SongCollision.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 301-422):

```text
    // RESIDUAL (w7-ak, 94.2 canonical): 80 rows, 41 of them one r28<->r29
    // rotation plus an r15<->r16 rotation.  The visible cause is here: the image
    // keeps `this` in r30 (`mr r30, r3`) and only forms `this + 0x2c` AFTER the
    // Timer constructor returns (`addi r29, r30, 0x2c`), home-storing it into the
    // 0x50(r31) slot the early MILO_ASSERT argument buffers have just finished
    // with; binding `data` at function scope emits `addi r28, r3, 0x2c` as the
    // second instruction of the body and gives it a slot of its own at 0x64.
    // BOTH ways of expressing that are REFUTED, and both lose more than the 4
    // rows they win, because whatever holds the array base also decides where
    // `dancer` lives:
    //  - moving `auto &data = mData;` down to just after `Timer timer;` costs
    //    94.2 -> 92.4: `dancer` is evicted from r14 into r28 and picks up a home
    //    store of its own;
    //  - dropping the reference entirely and spelling `mData[i]` / `mData[0]` at
    //    the four use sites costs 94.2 -> 92.8.  It DOES close the (0xd0,0xf0)
    //    BeatCollisionData offset swap and takes the stack diff from 4/6 to 2/2,
    //    but `dancer` is evicted the same way (`stw r28, 0x64(r31)`), and the
    //    array base then lands in r14 instead of r29.
    // The r28/r29 rotation is therefore not reachable through the array base on
    // its own -- `dancer`'s allocation has to move with it.
    //
    // w26-k (2026-11-02): mechanism, from the c2 colour-allocator tap, plus a
    // correction to that last sentence: `dancer` is in r14 on BOTH sides in the
    // current tree (`lbz 11, 425(14)` at tgt 0x30C / ours 0x308), so it is not
    // what the rotation is about any more.  The whole knot reduces to one
    // fact: `this` is NOT a colour candidate in our build.  `addi r28, r3,
    // 0x2c` + `stw r28, 0x64(r31)` are the first two instructions of the body,
    // so c2 CSE'd `this + 0x2c` and dropped `this`; the image keeps `this` in
    // r30 (`mr 30, 3` at 0x24), forms `addi r29, r30, 0x2c` at the first mData
    // use (0x138) and home-stores THAT to 0x50(r31).  Everything downstream
    // follows: the two hoisted global addresses then take r28/r29 in the image
    // and r29/r30 in ours, which is the whole r28<->r29 rotation (20 rows),
    // and the spurious home store at 0x64 is the (0xd0,0xf0) offset swap.
    // The second rotation, r15<->r16 (11 rows), is an independent tie-0 pair:
    // one hoisted address half and one `li 0` zero constant.  Ours pops the
    // address first (id 58, pri -68 tie 0, code band -> r16) and the k0d zero
    // second (id 4, pri -71 tie 0 -> r15); the image needs the zero popped
    // first, so address -> r15 and zero -> r16.
    // Behaviour re-read against the image listing: same calls in the same
    // order, same stores, same Set(minX, maxX) / push_back /
    // bones_min_max_x sequence; objdiff diff_op none; unicorn EQUIVALENT at
    // harness h4.  The two `MakeString` rows under Function Call Diff are the
    // per-TU assert-instantiation class, not a defect: ICF binds one arbitrary
    // `MakeString<char[N],int,char[M]>` spelling at the folded address: the
    // image shows ONE (`char[19]` file, `char[4]` expression), we emit nine
    // (all `char[18]`, one per distinct expression length), so that row is a
    // naming artifact of the fold and not reachable from this source.
    //
    // w7-ba (floor held at 94.2; four more spellings measured).  The image's
    // shape is now pinned down further, and the knot is a single allocator
    // choice:
    //  - the dead `stw r29, 0x78(r31)` at 0x8251127C, right before the size()
    //    loads, is a NAMED reference bound to `mData[0]` after the loop (a ref
    //    bound to `data[0]` through the function-scope ref is folded away and
    //    byte-identical to this spelling).  With `mData[i]` in the loop and
    //    `std::vector<BeatCollisionData> &easy = mData[0];` at the end, that
    //    store, the r15/r16 order, the 0xd0/0xf0 bcd slots and the 0x88/0x90
    //    Timer::Ms temps all match (281/349 rows equal vs 265/345 here), but
    //    canonical reads 92.8 because `dancer` is still evicted: the hoisted
    //    `this + 0x2c` temp takes r14 and dancer takes r28, the register
    //    TheHamDirector@ha freed, which is the one `beat` then steals inside
    //    the inner loop (image: dancer r14, base r29 = the freed register,
    //    base spilled to 0x50 and reloaded at 0x82511190/98).
    //  - a pointer local after `Timer timer;` is byte-identical to w7-ak's
    //    reference move (92.4); a per-difficulty reference inside the loop body
    //    is byte-identical to the `mData[i]` spelling.
    // What is left is which of {dancer, hoisted base} gets r14; nothing in the
    // source order tried so far moves that.  Kept the higher-canonical spelling.
    //
    // w21-g (floor held at 94.2, 80 rows; behaviour re-read against the image
    // listing 0x82510D98-0x825112C8 -- Set(minX, maxX) argument order, the
    // maxX/minX resets, bones_min_max_x(minX, maxX, ...), the 0x48 size and the
    // 1/30 frame scale all agree; no defect).  One more spelling measured:
    // `auto &data = mData;` declared right after `Timer timer;` but used ONLY
    // for the final `data[0].size()`, with `mData[i]` in the loop -- the image's
    // home slot 0x50 for the base suggests exactly that lifetime -- 92.4, and
    // `dancer` is evicted to r28 with a home store at 0x68 (same failure as
    // w7-ak's move).
    // w21-al (floor held at 94.2): a standalone /FAs probe of this TU scored
    // with objdiff against the target object reproduces every number above
    // (94.24 / 92.79 / 92.44), so the probe is faithful.  Thirteen more
    // spellings, none above 94.24: `easy = data[0]` at the end with `data` at
    // function scope (byte-identical) or after the Timer (92.44); `data`
    // bound before the Timer / before usefulBones (91.67), before `dancer`
    // (93.90), inside `if (moveDir)` or before the first MILO_ASSERT (93.23);
    // a function-scope `std::vector<BeatCollisionData> *` (identical); with
    // `mData[i]` + `easy`: function-scope `int i`, `HamCharacter *const`,
    // a cast WorldXfm call, end()-begin() for size, mData[0].size() direct
    // (all 92.79-92.82).  The Timer::Ms rows (sradi/clrrwi order) come from
    // the PCH inline CyclesToMs and were not touched.
    // w21-bm (floor held at 94.2, 80 rows; same /FAs probe, 30 more spellings,
    // none above 94.24).  The stack layout says what the image did: its base
    // value is an UNNAMED compiler temp spilled to 0x50 (pooled with the
    // assert line temps), which is why minX/maxX sit together at 0x60/0x64
    // and the DifficultyToSym Symbol temp at 0x78 -- whereas the named `data`
    // here gets its own home at 0x64 and pushes minX down onto 0x50.  An
    // unnamed base (`mData[i]` + `easy`) reproduces the slots but MSVC then
    // spills `dancer` instead of the base, every time: invariant (92.79 to
    // three places) under loop-local vs outer Transform/minX/maxX, decl order
    // of current_beat/startXfm/minX/maxX, a renamed bcd, an explicit (int)
    // on SecondsToBeat, a literal 4 bound, while(it){...++it;}, an enum
    // loop counter (88.6), a dancer declared at function scope or split
    // decl/assign, a WorldXfm reference local, RndTransformable::WorldXfm(),
    // a function-scope pointer assigned after the Timer (92.44).  Timer::Ms
    // rows: the image's sradi/clrrwi order varies per caller (CameraTilt vs
    // FlowWhile), so it is context scheduling in the PCH inline.
    // w29-b (C2RS-BRIDGE 8.6, one-basic-block demotion): the c2-rs tap shows
    // `this` being DEMOTED (CDEMOTE 85 -> temp) because this reference forms
    // `addi rN,r3,0x2c` in the ENTRY block; declaring it at function scope also
    // costs a 4-byte frame var, since the base home-stores it to 0x64 and that
    // pushes minX onto 0x50.  The image keeps `this` in r30 and forms the same
    // address at the loop preheader instead (`addi r29,r30,0x2c` at row 79,
    // after the GatherUsefulBones/Timer calls) and home-stores it to 0x50.
    // Moving this declaration inside the if-block, just after `Timer timer;`,
    // reproduces the image's entry block byte-for-byte (rows 0-78 all equal,
    // the early `addi`/`stw 0x64` both gone) and lands the address at the same
    // row 79 -- but in caller-visible r14 with NO home store, so the cascade
    // down the loop is worse: 92.42730 / fuzzy 91.89317, base 337 = target 337
    // instructions, vs 94.19585 / 93.52819 with base 335.  So the higher score
    // here comes from a body two instructions SHORTER than the image (bridge
    // 14), and the remaining handle is c2 choosing a callee-saved home over the
    // image's stack home for a value whose live range spans the loop's calls.
```


## `?CheckCollision@SongCollision@@ABAXHQBW4Difficulty@@QBVTransform@@AAUSongCollisionOutput@@@Z`

`SongCollision::CheckCollision` -- moved verbatim from `src/system/hamobj/SongCollision.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 488-518):

```text
    // w16-b (99.943, 23 rows = IV bump order + FPR permutation): a plain
    // `for (i = 0; i < 2; i++)` and a Transform struct assignment in place of
    // the memcpy are both byte-identical to this.
    // w21-aw (23 -> 12 rows): binding `const Transform &xfm = transforms[i]`
    // once fixed the induction-pointer bump order, and Length(dir) written
    // out fixed its sum order.  Stop: the 12 left are the min/max diff loads
    // (f12/f13 permutation, image loads xfm.v.y before minEdge->y), the min
    // projection's commuted fmuls/fmadds operands (image normalDir first),
    // and the max projection's term order (image z, y, x; ours z, x, y).
    // Inert or worse: products commuted (`minDz * normalDir.z`, inert);
    // explicit `(z + y) + x` / `(y + z) + x` on the max projection (sorted
    // to y, z inside the group); Vector3 minDiff/maxDiff by member stores in
    // this order (98.8 -- operands then come out normalDir-first, but the
    // walker bases move) or by the 3-float ctor (98.5); the image's own issue
    // order minDz, minDy, minDx, maxDx, maxDz, maxDy (98.4, see below).
    // w26-c: read the 12 rows off the c2 tap rather than by probing.  The min
    // projection's products are two VARIABLE leaves and the emitted order is
    // the post-lowering commutative sort (C2RS-BRIDGE 8.7), "higher sid
    // first": ours is (minDz sid 24, normalDir.z sid 22) -> minDz first, and
    // the image needs normalDir.z first, i.e. normalDir.z must key ABOVE the
    // diffs.  Its sid cannot rise above a later-declared diff, so the image's
    // normalDir component is either a temp leaf (0x10000|sid<<6, which beats
    // every named local) or a memory leaf -- both of which are about how c2
    // classifies the hoisted `normalDir` component load, not about how the
    // expression is written.  Measured inert here (129/141 unchanged):
    // reference aliases of normalDir (single and double binding), float
    // references to its components, both with the explicit pair parentheses,
    // and reference aliases of *minEdge/*maxEdge.  NB aliasing normalDir is
    // actively worse (121); the w21-aw note's "Vector3 minDiff comes out
    // normalDir-first at 98.8" is the one spelling seen to reach this order,
    // and it costs more elsewhere than it buys.
```
