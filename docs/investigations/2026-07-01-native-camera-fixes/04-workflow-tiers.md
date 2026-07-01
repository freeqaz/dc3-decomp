# Workflow tiers — how these fixes are being driven

Multi-agent (ultracode) pipeline. Each tier is a `Workflow` run; the orchestrator
reads each tier's result and gates the next. Agents can build and run the GPU port
(skip sandbox) and self-validate visually; the orchestrator does a final visual gate
before landing.

Concurrency is capped (≤ ~6 effective); worktree isolation is used wherever agents
mutate files in parallel.

## Tier 1 — Planning / Discovery

Goal: a concrete, validated implementation plan for **both** bugs.

- **Bug 1 (design):** agents read `UI.cpp` (UI-cam modes), the UI draw path,
  HamNavList / HamListRibbon, and every on-screen text consumer; enumerate the
  world-Z extents of each UI element group (main-menu labels, choose-mode labels,
  help-bar, dialogs, HamNavList ribbons) by instrumenting `WorldToScreen`; propose
  the HD-framing fix that shows **all** groups at once (resolving the Z=387 vs
  HamNavList conflict). Screenshot-verify the candidate.
- **Bug 2 (discovery):** agents reproduce the gameplay camera flip (GPU, sandbox
  skip), capture consecutive gameplay frames + `[CAM]` debug log, read the camera
  machinery (`CameraManager::Poll`, `Cam.cpp`, engine camera-change path), and
  root-cause it. Especially check whether `311e3b75` also destabilised the gameplay
  camera.
- **Judge/synthesis:** a panel scores candidate fixes and emits a single plan per
  bug: files to change, exact approach, risk notes, acceptance criteria.

Output → `05-plan.md`.

## Tier 2 — Implementation

Goal: working patches, self-validated.

- Runs in a **DC3 worktree** (`scripts/setup_worktree.sh`) so the main repo is
  untouched. Implement both fixes per the plan; keep changes `HX_NATIVE`-guarded /
  PPC-neutral where possible.
- Build native in the worktree, capture before/after screenshots on the acceptance
  screens, iterate until they pass.
- Output: the diff + before/after screenshot paths + a short implementation note.

## Tier 3 — Review (adversarial)

Goal: catch defects before landing.

- Independent agents review the patch for: correctness, native-safety (no null
  derefs / new asserts on flows the fix touches), **PPC-neutrality** (run_objdiff on
  any non-`HX_NATIVE` function touched — must not regress match%), and a
  **UI-regression sweep** (screenshot attract/title/main/choose_mode/song_select/
  gameplay/a dialog — nothing else broke). Findings are adversarially verified before
  reporting; rank by severity.

Output → `06-review.md`.

## Tier 4 — Refinement + Landing

Goal: shipped.

- Apply confirmed review fixes; re-validate visually (orchestrator final gate).
- Run the `milo-tests` suite (must stay green) and confirm the decomp/report plane is
  unaffected (changes are `HX_NATIVE`-guarded).
- Commit on a branch in the worktree with clear messages (**no `Co-Authored-By`**),
  then merge/hand off to `main`. Record the landed commits here.

Output → `07-landing.md`.

## Runtime-verification is non-negotiable

Camera/projection/UI-layout fixes cannot be certified by unit tests alone. Every
tier that changes behavior must attach screenshots proving the actual pixels. The
acceptance screens for bug 1 and the consecutive-frame gameplay capture for bug 2
are the certificates.
