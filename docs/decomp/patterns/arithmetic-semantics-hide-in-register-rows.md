# Wrong arithmetic hides in register-only rows — and the canonical ruler forgives them

**Written 2026-09-30** (lane det-arith). Detector: `scripts/analysis/arith_semantics_scan.py`.
Taxonomy class 4 (`docs/sessions/2026-09-15-two-month-native-impact-bug-review.md`,
68 historical bugs, no detector until this one). Companion to
[rounded-100-hides-real-bugs.md](rounded-100-hides-real-bugs.md) and
[wrong-field-at-100-percent.md](wrong-field-at-100-percent.md).

## The mechanism

The brief for this lane assumed class-4 bugs *cost match points* and were merely
drowned in noise. That is half true. An opcode substitution (`srawi`/`srwi`,
`fmadds`/`fmsubs`, `lha`/`lhz`) does cost points. But the commonest shape found
on this pass costs **nothing on the canonical ruler**, because the only
difference is **which register an otherwise identical instruction reads**:

| function | image | ours | canonical before |
|---|---|---|---|
| `ObjectDir::ResetViewports` | `stfs f13, 0xd8(r31)` (f13 = -1.0) | `stfs f0, 0xd8(r31)` (f0 = +1.0) | 98.646614 (row not charged) |
| `SkeletonClip::LoadFrame` | `stfs f13, 0xc(r30)` (1.0) | `stfs f0, 0xc(r30)` (0.0) | **100.0** |
| `NgSpotlightDrawer::SetupForPostProcess` | `stfs f13, 0x64(r1)` (far plane) | `stfs f31, 0x64(r1)` (0.0) | **100.0** |
| `PoseFatalities::EndFatal` | `subfe r10, r10, r10` | `subfe r10, r8, r10` | **100.0** |
| `DataNode::Equal` | `subfe r11, r11, r11` | `subfe r10, r8, r10` | 99.19192 (row not charged) |
| `RndMorph::SetFrame` | `subf r9, r30, r31` | `subf r9, r31, r30` | 99.09091 (row not charged) |

Canonical forgives register permutation by design, and a *different register*
is indistinguishable from a *permuted* one at the row level. Only `fuzzy`
charges these rows, and a function whose residue is "register noise" is exactly
the one nobody reads. The value difference lives one step back: **where the
register was defined**.

- `subfe rD, rX, rX` is `CA - 1` (a 0/-1 mask of NOT-carry); `subfe rD, rA, rB`
  after `subic rA, rB, 1` is `CA` itself. Same carry, **opposite truth value**,
  register-only diff. `b &= x != 0` vs the image's `if (x) b = false;` is this.
  **Lever:** a conditional flag *clear* (`if (c) flag = false;`) is what MSVC
  if-converts into the image's `subic/subfe rX,rX` mask; `flag &= !c` / `== 0`
  emit `cntlzw/extrwi` instead (EndFatal: 99.35 → 100.0).
- A reversed `Scale(src, f, dst)` / `subf a, b` shows up as a swapped
  `subf` whose two operands are the *same two values* crosswise.

## What the tool does

One sharded `objdiff-cli diff --batch --include-instructions` pass over every
function in `report.json` (universe **48,365**; the not-defined **16,072** are
dropped by name). Each aligned row is canonicalised (`subi`→`addi -imm`, every
`rlwinm` alias → (rotate, mask32)) and classified; register-only rows are traced
back to their **defining instruction on each side** and compared by a bounded
value-numbering (`val_eq`) that follows reloads from stack slots to the last
store. See the docstring for the full bucket list; the reported ones are
`signedness`, `int-width`, `float-width`, `float-sign`, `int-op`,
`operand-order`, `operand-source`, `const-operand`, `op-substitution`,
`net-term`, `cond-mask`.

**Measured 2026-09-30, whole binary** (branch `det-arith`, after its fixes):
universe **48,365** report functions; examined **32,220** (66.62%); dropped
16,072 not-defined-in-our-build, 58 objdiff errors, 14 duplicate names that
batch mode resolves to another unit, 1 objdiff hang (`?Terminate@VirtualKeyboard@@QAAXXZ`,
named in the coverage block). **1,104** examined functions have any mismatch
row; **190** carry a reported row. Reported rows: signedness 3, int-width 5,
float-width 1, float-sign 16, int-op 12, operand-order 1, operand-source 363,
const-operand 63, op-substitution 65, net-term 48, cond-mask 24. Counted:
register-only 12,384, operand-exchanged 821, displacement 2,538, stack 2,418, ...
~2 min wall with 12 workers.

**Two-sided sabotage control** (re-introducing the historical `Rand::Seed` bug,
`((unsigned int)j >> 16)` → `(j >> 16)`, full `ninja` each way): clean
signedness **3 rows / 2 functions** → sabotaged **4 / 3** with
`?Seed@Rand@@QAAXH@Z` as `srawi 16 vs srwi 16` → reverted **3 / 2**, stdout+stderr
byte-identical to the clean run (135,109 B, exit 0 all three). ⚠ **The first
attempt at this control FAILED**: objdiff rendered the one-opcode change as a
delete + insert, the row landed in `multi-atom`, and signedness stayed 3. The
fix — pair one-sided instructions through the substitution table when both
operate on the same input value — is what made the control pass. Determinism:
agreed with itself across `PYTHONHASHSEED` 1/7 on 135,109 B.

`cond-mask` is also the **blind spot of det-cond's** `cond_semantics_scan.py`:
a condition compiled *without a branch* (the carry-chain idioms) is arithmetic.
`subfe` masks and `cntlzw/extrwi` zero tests are evaluated to a truth predicate
on both sides; equal truth is `cond-mask-equivalent` (counted), which is what
the five objdiff `BOOLEAN_NEGATION` rows det-cond checked turned out to be.

## Measured noise (each became a counted bucket — do not re-raise)

- **exchanged components**: x- and y-component computations aligned crosswise
  (`fadds f13,f10,f12` / `fadds f0,f11,f0` vs the other way round). Operand
  values are cancelled as a multiset (`operand-exchanged`).
- **operand loads in the opposite order** (GlitchPoker::Dump): the alignment
  pairs each load with the other one. Value-numbering, not alignment, decides.
- **a swapped subtraction consumed only by a zero test** (`sSelected == this`).
- **sign folded into a literal**: `x2*3 + x3*(-2)` vs `x2*3 - x3*2`
  (RndTexBlendController::GetBlendState, ResetViewports' `±768`).
- **static-guard / destructor-flag bit numbers** (`?$S9@` guard bit 2 vs 1,
  `li r28,2` vs `1`): a per-function counter.
- **`extrwi. 1,b` vs `rlwinm. 0,b,b`**, `addi 4; addi 4` vs `addi 8`,
  `clrlwi 24` insertion (BOOL_MASK), `clrrwi rD,rS,0` (a move).
- **0/-1 vs 0/1 truth masks** with the same predicate (IsGameScreenActive's
  current spelling, HDCache::Init): equivalent when ANDed into a bool.
- `/fp:fast` reassociation, FMA contraction and reciprocal folding: balanced
  by splitting fused ops into atoms; an `fdiv` imbalance is `reciprocal-fold`.

## What the tool CANNOT see

- **Functions we do not define** (16,072): arithmetic arrives with the body.
- **A dropped STORE** (`DxRnd::SavePreBuffer`'s `w = 0` — fixed on this branch,
  found by hand): not an arithmetic atom. That is class 10.
- **Definitions across a join — FIXED 2026-09-30 (arith-tail).** The def walk
  was linear (and gave up after 256 instructions); in branchy functions it
  picked another arm's definition (`SaveLoadManager::SetState`'s `MemFree`
  file-name register). It now resolves the UNIQUE reaching definition over
  each side's CFG, and a row whose operand has no unique one goes to the lead
  bucket `trace-crossed-branch`. Same objects, 357 `operand-source` rows: 242
  stay, 76 → `trace-crossed-branch`, 39 dissolve. The earlier claim that every
  true positive sits in a function with ≤ 20% mismatch rows did NOT survive
  the tail: `MoveDir::UpdateOverlay` (49%) and `Hmx::Object::OnAddSink` (27%)
  carried real bugs. Sort order is a priority, not a filter.
- **Resolving the definition is not resolving the value.** Scheduling that
  pairs one component's multiply with another's (`RndScaleObject`), and two
  `fmr` copies of different function inputs (same exchange key), still
  surface as `operand-source`. So does a call the tool assumes clobbers every
  volatile register when MSVC kept one live across a same-TU leaf callee
  (`PropSync<ObjVector<Strand>>`, r6 across `Strand::Strand`).
- A wrong value produced by the *same* instruction sequence (a wrong field
  feeding the op — class 1; a wrong `.data` constant — class 5).

## The operand-source tail, adjudicated (2026-09-30, branch `arith-tail`)

341 of the 357 rows (131 of 133 functions) that `operand-source` carried after
det-arith's fixes were hand-traced on both sides, respecting control flow (sbs listing =
`objdiff-cli diff --batch --include-instructions`, plus symbolic/numeric
per-path store simulators for the float-heavy ones). **Seven were real bugs,
plus one unflagged bug found beside a flagged row:**

| function | wrong behaviour | fix |
|---|---|---|
| `Hmx::Object::OnAddSink` | empty event list with explicit chain = 0 registered chainProxy = false; image `li r28, 1` on that arm | pass `true` |
| `MoveDir::UpdateOverlay` | overlay row pitch cached from the text WIDTH (`result.x - pos.x`); image reads `.y` at 0x64(r31) minus y | `(result.y - y) * 0.8f` |
| `MoveDir::UpdateOverlay` (unflagged) | we stripped a leading '/' from the move name; image passes `Name()` | drop the guard |
| `NgPostProc::RebuildTex` | all three bloom levels w/4 x h/4; image re-divides at the loop head (w/4, w/16, w/64) | divide inside `BloomTextures::AllocateTextures` |
| `MoveDir::DetectFrac` | autoplay: `i7 / (i8 * frac)` — divides by the rating; image `(i7 / i8) * frac` | multiply |
| `RndShaderDrawRect::CalcShaderOpts` | mask `0xAFFFFFFE << 22` zeroed the diffuse/prelit bits; image rotl64(sign-extended 0xAFFFFFFE, 22) clears only bits 22/50/52 | `~(bit22 \| bit50 \| bit52)` |
| `Hmx::Object::ReplaceRefsFrom` | `other.AddRef(it)` spliced the temp head INTO mRefs; ReplaceList then retargeted every ref | `it = it->MoveBefore(&other)` |
| `SkeletonViz::Visualize` | tracked path never re-selected the caller's camera | Select after the if/else |

Two of the eight were **control-flow** bugs (RebuildTex's loop head,
Visualize's branch targets) that surfaced as a register-only row because the
saved value lived in a register the wrong path reused — a register-only row is
a lead into any class, not only arithmetic.

**Refuted — reasons, so the next lane does not re-raise them** (row counts in
brackets; ⊘ = not in the native build):
- *linear trace crossed a branch/join; unique reaching defs agree*:
  SaveLoadManager::SetState [8], DataNode::Equal [1], HolmesClientOpen [5],
  MemHeap::TryAlloc [2], FindVITargetTypeInstance⊘ [2], yy_get_next_buffer [4],
  OSCMessenger::GetInt [1], RndText::WrapText [3], DepthBuffer3D::DrawShowing [3],
  HamScrollSpeedIndicator::Update [1], EQEffect::SetParameter [3],
  HamAudio::PollCrossfade [2], WordWrap_CanBreakLineAt [1], HamNavProvider::
  OnSetEnabled/OnSetHidden [1+1], Rnd::TestPoint [2], UpdateBufferTex [3],
  HamNavList::NumItems [1], RandomGroupSeqInst ctor [2], MoveDir::Poll [2],
  TypeProps::Save [1], Locale::Init [1], StorePanel::LoadArt [1].
- *exchanged components / scheduling / reassociation (/fp:fast, FMA)*:
  RndParticleSys::InitParticle [3] & MoveParticles [2], ResetNormals [1],
  CharCollide::GetRadius [2], CharEyes::EnforceMinimumTargetDistance [1],
  SpotlightDrawer::DrawWorld [2], HamDirector::Poll [1], Synth::DrawMeter [1],
  ArcDetector::DrawPath [2] & GetPathError [1], RndAmbientOcclusion::
  SmoothResults [1] & BlendVert [2], RndFlare::CalcRect [1], CharBones::
  RotateBy [1], HandInvokeGestureFilter::CalcInPose [3], BaseSkeleton::
  MakeCameraToPlayerXfm [4], RndTransformable::ApplyDynamicConstraint [5],
  CharIKFingers::CalculateHandDest [2], BSPFace::Update [3], Intersect [3],
  CharForeTwist::Poll [2], EQEffect::Process [7], RndLine::UpdateLine [10] &
  UpdateLinePair [4], MakeScale [1], Spotlight::BuildNGSheet [8],
  IsValidSwipePosition [1], Vector3DESmoother::Smooth [1], FlangerEffect::
  Process [2], HamRegulate::Regulate [1], RndWind::SelfGetWind [3],
  HamRibbon::UpdateChase [1], CharGuitarString::Poll [1], Quat::Set [2],
  BurnXfm [1], Multiply(Vector3,Transform) [2], HamIKEffector::
  ComputeElbowPullAndQuat [2], TransformKeys [2], CharIKHand::IKElbow [10],
  HandAtSide [1], DrawDetectedBar [1], UtilDrawPlane [3], TransformNormal [4],
  BoxMapLighting::ApplyQueuedLights [3] & CacheData [1], Spotlight::
  BuildNGQuad [4], NgSpotlightDrawer::RenderConeDefs [4] & SetupXSection [6]
  (both `{}` natively), Multiply(Vector3,Quat) [3], Multiply(Transform,
  Transform) [1], Multiply(Matrix3,Matrix3) [5], NgLight::SphereConeTest [9],
  Invert(Matrix4) [17] (host-compiled: M·out = I to 2.2e-7 over 1,000
  matrices), RndScaleObject [1, new with the CFG], and MoveDir::UpdateOverlay's
  other 5 rows (fmadds reassociation; one sLightGray pointer reloaded from two
  spill slots; quotient/remainder crossed between registers).
- *induction-variable / index spelling (same faces, same indices)*:
  Spotlight::BuildCone [7] & BuildBeam [4], Skeleton::Poll [2],
  EQEffect::Reset [1], BustAMovePanel::RepsToNextPhrase [2], HamRibbon::
  ConstructMesh [1], RndRibbon::ConstructMesh [2], TessellateMesh [3],
  RndAmbientOcclusion::Tessellate [5], RndBitmap::PixelOffset [1],
  fft_scalar⊘ [3], fft_matrix_inverse_columnwise⊘ [1], PackVector⊘ [1].
- *misaligned pairing / renamed registers, same arguments*:
  RndMat::UpdatePropertiesFromMetaMat [1], RndColorXfm::AdjustSaturation [2],
  JoypadPollCommon [1], ClipPlayer::AnnotateClip [1], DepthBuffer3D::Load [1],
  CharClip::Transitions::AddNode [1], StorePanel::OnMsg [1] (bool mask),
  HamSkeletonConverter::SetLeg [3] (0x734(r11) == 0x4(r30)), FlowCommand::
  Load [1] (list declaration order — a match lever), PartyModeMgr::
  CreateEventA [1] (vec[i] vs its copy — a lever), MemTracker::DiffDump [1],
  PropSync<ObjVector<Strand>> [2] (r6 live across a same-TU leaf call).
- *equal truth masks / bit spellings*: HDCache::Init [1],
  RndShaderSimple::CalcShaderOpts [1], RndShaderParticles::CalcShaderOpts [1],
  RndSpline::SyncPristineCtrlPoints [1] (two carry spellings of max(x,0)),
  DecodeDxt5Alpha [1], ReadFunc⊘ [1] (bswap64 simulated), FillCompressedVertex⊘ [1].
- *same address, different anchor*: dprintf_formatf [1], MemAlloc [1]
  (`&gNumHeaps - 0x294` = `&gHeaps`), SpectralAnalysis::Analyze⊘ [1],
  MemcardMgr::ThreadStart⊘ [1], MemFindHeap [1] (strcmp operands, == 0 only),
  ArkHash::Read [1], MeasureMap::AddTimeSignature [1] (CSE),
  NavListSort::ChangeHighlightHeader [2] (differs only after MILO_FAIL).
- *equal by path / out-of-line vs inline*: BinStream << vector<TransformCrowd> [1],
  Voice::createOrReuse⊘ [2], ChatReceiver::ProcessChatData⊘ [3],
  MemcardXbox::ShowDeviceSelector⊘ [1], fft_matrix_forward_columnwise⊘ [3],
  fft_real_forward_altivec⊘ [1].
- *behaviourally verified*: CSHA1::Transform [18] (unicorn, both .text
  sections produce SHA-1("abc"); see SHA1.cpp).

- *hand-traced every arm*: Spotlight::BuildNGCone [22] (orientMtx at 0xb0
  vs 0xe0 — the swapped slot the source already records; all three
  matrix-multiply sites and every face index equal).
- *new rows after the CFG change, refuted*: RndSoftParticleBuffer::BlurSurface,
  BaseDisplacementNode::Displacements, WorldCrowd::SetFullness, and
  DepthBuffer3D::DrawShowing's three `fmr` clamp rows (both listings emulated
  over 20,000 random inputs, 0/120,000 output mismatches; a one-opcode
  sabotage of ours gives 2,000/2,000).

**Not adjudicated (16 rows, both ⊘):** `fft_altivec` [13] — structurally
divergent (image 762 instructions to our 584; 94 vs 58 `vmaddfp`; the image
builds `vperm` control words where we store masks) — needs a VMX emulator and
a whole-function harness, not a row trace. `fft_recursive` [3] — one image
`vmaddcfp128` (vD = vA·vD + vB) against our `vmaddfp128` (vD = vA·vB + vD),
equal only if the accumulator and addend swapped roles, unconfirmed.

## The `trace-crossed-branch` lead bucket, adjudicated (2026-09-30, lane leads-disp-arith)

Whole binary at `731b6137f` (main): **84 rows in 46 functions**, byte-identical
(as a multiset of symbol / target / ours rows) to arith-tail's final scan. 71
of the 84 are rows arith-tail had already read as `operand-source` before its
CFG change; 13 are new with the CFG trace. The bucket had never been read AS a
bucket, so this pass re-read every native row independently, testing the
earlier reasons as hypotheses (two agents plus the lane, each row traced on
both sides along every reaching definition, joins and back edges included).

**Adjudicated 69 of 84 rows (68 native + 1 ⊘); 0 real bugs.** Precision of the
bucket on this tree: **0 / 69**. Not adjudicated: the **15 ⊘ FFT rows**
(`fft_altivec` 8, `fft_real_forward_altivec` 3, `fft_matrix_forward_columnwise`
2, `fft_matrix_inverse_columnwise` 1, `fft_recursive` 1) -- synth_xbox, not in
the native build, VMX128 (no emulator for it here).

What the rows actually were (69 rows, each in exactly one class) -- read this before trusting the bucket's name:

| class | rows | examples |
|---|---|---|
| **objdiff paired two different instructions** (often `mr` into DIFFERENT argument registers: `mr r3, x` vs `mr r5, y`) | 19 | AdjustSaturation [2], HamNavProvider::OnSetEnabled/Hidden, AnnotateClip, CharClip AddNode, PixelOffset, TypeProps::Save, MakeCameraToPlayerXfm, TransformKeys, BuildBeam [2], RndRibbon::ConstructMesh [2], UpdateLine [2], BuildNGCone [3] |
| the scanner's "other definition" sits on a path that cannot reach the row (jump-table arms, an exit path, the arm of the other `if`) | 14 | SaveLoadManager::SetState [8] (all 19 sites have ONE reaching def once the `bctr` fan-out is modelled), RandomGroupSeqInst ctor [2], WrapText, MoveDir::Poll [2] (a spill of the same `&TheTaskMgr`), UpdateOverlay [1] |
| induction-variable / index spelling | 10 | BuildCone [4] (0x60·i both ways), EQEffect::Reset, RepsToNextPhrase [2], Tessellate [2], BuildNGQuad |
| exchanged components / commuted operands / reassociation | 10 | MoveParticles [2], BuildNGSheet [2], EQEffect::Process [3] (also numeric, both `.text`s, 70 inputs: 1.4e-5; one sabotaged `fmadds` of ours: 3.31), CalcRect, Displacements, ApplyQueuedLights (a different accumulator spilled per iteration) |
| equal masks / equal values by path | 10 | HDCache::Init, RndShaderParticles::CalcShaderOpts, StorePanel::OnMsg, Voice::createOrReuse⊘, SetFullness, DepthBuffer3D::DrawShowing [2] (now traced upstream of the clamp too), BlurSurface, UpdateLine [2] (a dead `addi`; the loop pointer equals `end` on both exits) |
| other: list declaration order (FlowCommand::Load), strcmp operand order tested `== 0` only (MemFindHeap), out-of-line vs inlined Save (BinStream << vector<TransformCrowd>), a register renamed around an unchanged member reload (MemTracker::DiffDump), UpdateOverlay's colour pointer (image `.data lbl_82F0EB08` = ours `sLightGray` = {0.8, 0.8, 0.8, 1}), NavListSort (below) | 6 | |

The two UpdateOverlay rows were the only native rows no one had read:
`CurrentMoveMode()` is called earlier in the image and spilled (0x68(r31)), later
in ours (r22) -- both before the row loop that consumes it, and nothing between
the calls changes the mode; and the `DrawStringScreen` colour is the same
{0.8, 0.8, 0.8, 1} under two names.

**One real control-flow difference, deliberately left alone:**
`NavListSort::ChangeHighlightHeader` -- when the header does not move, the
image skips its inlined wrap (`Mod`) and we always apply it. Values agree for
every `shortcutIdx` in `[0, size)`; they differ only after
`GetCurrentShortcut` has already hit its MILO_FAIL and returned -1, where the
image reads `mShortcutNodes[-1]` and we wrap to `size - 1`. Restoring the
image's shape would restore an out-of-bounds read on an already-failed path.
(The source comment's claim of the "same block layout" is not literally true.)

**What this says about the bucket.** Its premise -- a non-unique reaching
definition -- is real, but on this tree it is dominated by two things the
scanner could model instead of reporting: (1) the pairing -- when the two
instructions write different registers, the row compares different values by
construction, and the meaningful comparison is each consumer's register (the
call's r3..r10), not the paired instruction's source; (2) jump tables -- a
block reached only through `bctr` has "unknown" predecessors, which makes every
switch-heavy function (SetState's 8 rows) ambiguous. Neither was changed here:
(2) is only sound if every jump-table target is a block leader, and a case that
falls through into the next case can be entered mid-block, so it needs the
table's contents, not a guess.

## Manual recognizer (for the part the tool cannot reach)

1. In a sub-100 function, read every `diff_arg` row whose opcode is a **store**
   or a **carry op** (`subfe`, `adde`, `addze`) even when objdiff calls it
   register-only. For a store, find what each side's source register was last
   loaded from; for `subfe`, check whether rA == rB on each side.
2. For a swapped non-commutative op, check whether the two operand *values*
   are the same pair (then it is a real swap) or merely renamed.
3. Before calling any row a bug, state the wrong runtime value. `add`/`or` on
   disjoint bits, a 0/-1 vs 0/1 mask ANDed into a bool, and a reassociated float
   sum are all equal at runtime.
