# DTA Script Execution, Provider Wiring, and HamNavList Rendering

## Scope

This note is for the `docs/native/` effort. It is specifically about the native boot path reaching `choose_mode_screen` while the menu list is missing, empty, or visually absent. It should be read as a native-port triage note, not a generic decomp note.

## Status

Partially understood, but the original note was too narrow. We have enough evidence to say:

1. DTA does execute on native.
2. `choose_mode_screen` is not blocked on "no DTA system" anymore.
3. The remaining failure could be in one of four separate layers:
   - resource dir loading
   - provider binding/population
   - DTA command side effects short-circuiting
   - draw/composition making a populated list effectively invisible

The next session should treat those as separate hypotheses and prove or eliminate them one by one.

## Fresh Runtime Findings (2026-03-10)

Headless native run with `MILO_DEBUG_CHOOSE_MODE=1` changed the picture substantially.

### What the run proved

1. `ui/resource/lists/list_choose_mode.milo` resolves correctly as a `UIListDir`.
2. `choose_mode` starts with a 4-item `HamNavProvider`, then DTA upgrades the live provider to `ChooseModeProvider`.
3. `ChooseModeProvider::UpdateList(false)` does run on `choose_mode_panel.enter` and produces 5 items (`perform` first).
4. After DTA finishes, the live choose-mode list state is healthy:
   - provider = `ChooseModeProvider`
   - `numData = 5`
   - `numShowing = 5`
   - `widgets = 10`
   - `ribbonStates = 5`
5. Frame 500 still shows `choose_mode_panel` drawing through the shared `main` `PanelDir`, and that dir still reports only 4 draws.

### Most important new negative result

Frame-500 capture still does not show clear evidence of the 5-item choose-mode list actually contributing its own visible draw workload.

What the capture does show:

- `choose_mode_panel` still draws through the shared `main` `PanelDir`
- `PanelDir::DrawShowing 'main'` still reports only `draws=4`
- the frame-500 draw dump is dominated by background/ribbon/helpbar-style meshes (`mainMenuRibbon`, `color_tape`, `edge_soften`, warning/helpbar meshes, etc.)

What the capture does not show clearly:

- an obvious 5-item choose-mode list workload corresponding to the populated `ChooseModeProvider`
- easy-to-identify per-item meshes or labels for `perform`, `practice`, `dance_battle`, `custom_party`, `crew_showdown`

That keeps the focus on draw traversal / drawable registration rather than provider population.

### Revised conclusion from the run

The original "resource dir/provider population" hypothesis is no longer primary.

The likely failure has moved later in the pipeline:

- the `HamNavList` object is not in the active drawable set for the panel, or
- it lives in a subdir that is not being drawn/merged by the shared `main` `PanelDir`, or
- it is present but filtered before `DrawShowing()` is reached

### Additional widget-level findings from the same native pass

Later instrumentation pushed the failure point down another layer.

What is now directly confirmed:

1. `HamNavList::DrawShowing()` does run for `right_hand.hnl` on `choose_mode_screen`.
2. `UIListDir::DrawWidgets()` does run for the choose-mode list every frame after entry.
3. Generic slot draw state is sane at draw time:
   - `showing = 0..4`
   - `data = 0..4`
   - `alpha = 1.0`
   - `pos = (0, 0, -25*n)`
   - highlighted element state is stable
4. `ChooseModeProvider` is filling payload correctly:
   - `label` tokens are `perform`, `practice`, `dance_battle`, `custom_party`, `crew_showdown`
   - icon slots such as `icon_2p`, `icon_1p_plus`, and `icon_1por2p` resolve real mats where expected
   - only intentionally empty slots (`bloom_block`, some player-count icons) come back null

What this rules out:

- "widgets never draw"
- "provider filled blank labels"
- "all choose-mode mats are null"
- "HamListRibbon field punning is obviously corrupting `showing/data/pos` before widget draw"

What remains likely:

- the actual drawable payload from `UIListMeshElement` / `UIListCustomElement` / `UILabel::DrawShowing()` is not contributing visible output in the active pass, or
- panel/shared-dir traversal still excludes some list-owned drawables even though the `HamNavList` wrapper itself is executing

### Native-only cleanup worth keeping

`UIListDir::BuildDrawState()` was leaving parts of `UIListElementDrawState` uninitialized. On Xbox this was easier to get away with; on native it leaks stack garbage into any code that reinterprets the struct as ribbon overlay data. The native branch should keep zero-initializing those structs before filling fields.

## Direct Log Evidence Worth Preserving

- `ObjDirPtr::PostLoad end file='ui/resource/lists/list_choose_mode.milo' resolved=... class=UIListDir name=list_choose_mode`
- `HamNavList::SetNavProvider list=right_hand.hnl (ui/choose_mode/choose_mode.milo) ... items=4`
- `ChooseModeProvider::UpdateList done provider=main/choose_mode_provider count=5 first=perform`
- `HamNavList::SetProvider ... incoming=ChooseModeProvider ... numData=5`
- `HamNavList::RealRefresh end ... provider=ChooseModeProvider ... numShowing=5 ... widgets=10 ribbonStates=5`
- `ChooseModeProvider::Text data=0 sym=perform slot=label token=perform`
- `ChooseModeProvider::Mat data=0 sym=perform slot=icon_2p result=icon_2p.mat ...`
- `UIListDir::DrawWidgets provider=main/choose_mode_provider ... widgets=10 elems=5`
- `UIListSlot::Draw provider=main/choose_mode_provider widget=UIListLabel ... showing=0..4 data=0..4 alpha=1.000`
- frame 500:
  - `UIPanel::Draw 'choose_mode_panel' ... mDir=...(main)`
  - `PanelDir::DrawShowing 'main' ... draws=4`
  - draw dump dominated by shared-ribbon/background/helpbar meshes, not a clearly identifiable 5-item list workload

## Important Corrections To The Original Note

### 1. `choose_mode` is not just a `UIList` problem

The original note focused on `UIList::mListDir`, but `choose_mode` is more likely going through `HamNavList`, not a plain `UIList`.

Relevant native path:

- `HamNavList::PreLoad()` loads `mListDirResource`, `mListRibbonResource`, `mHeaderRibbonResource`, and `mScrollSpeedIndicatorResource`
- `HamNavList::PostLoad()` calls `PostLoad(nullptr)` on those resource dirs, then `Update()`
- `HamNavList::SetNavProvider()` chooses either a real `HamNavProvider` or falls back to `this` as a `UIListProvider`
- `HamNavList::RealRefresh()` does the actual `CreateElements()` and `FillElements()`

This matters because a null `UIList::mListDir` in some loaded subresource is not enough to prove the top-level `choose_mode` failure.

### 2. The async-race hypothesis is weaker than the original note suggests

`ObjDirPtr::PostLoad()` calls `TheLoadMgr.PollUntilLoaded(mLoader, loader)` before resolving the loaded dir. That means a plain "native forgot to wait for the loader" race is not the strongest first hypothesis. Path failure, wrong type, or later provider failure are more likely than a simple unresolved async load.

### 3. Searching draw capture by mesh name is weak evidence

"No choose_mode mesh names appear in frame capture" is not decisive. Many UI/text draws use generic or empty mesh names, and provider-driven labels do not imply meshes named `dance` or `story`. Keep capture analysis, but do not use mesh-name absence as proof that the list never instantiated.

### 4. `HamProvider` and `ChooseModeProvider` need to be separated conceptually

There are at least three relevant provider-ish paths here:

- `TheHamProvider`: global `PropertyEventProvider`, used for DTA properties/events
- `HamNavProvider`: nav-item list provider populated by DTA commands like `append_nav_item`
- `ChooseModeProvider`: explicit C++ `UIListProvider` with hardcoded mode symbols in `UpdateList(bool)`

These fail differently. Do not treat them as one subsystem.

### 5. Native already has a typed `hamprovider` baseline

`HamInit()` now ensures a named `hamprovider` exists on native and applies `HamProvider` type data when available. There is also a native test covering this. The next session should not spend time rediscovering or re-fixing `TheHamProvider == nullptr` unless a regression proves it broke again.

## What We Know

### Confirmed working

1. System/config DTA loads from ark on native.
2. Screen/object `HandleType()` handlers fire on native.
3. `choose_mode_panel` reaches its `enter` handler.
4. Native has a real named `hamprovider`.
5. `HamNavList` element creation timing was already fixed once in Session 37.
6. Native menu navigation works far enough that `choose_mode_screen` is interactive in later native sessions.
7. `list_choose_mode.milo` resolves successfully as a `UIListDir`.
8. `ChooseModeProvider::UpdateList()` runs during `choose_mode_panel.enter`.
9. The choose-mode list reaches a populated runtime state: 5 items, 5 showing, 10 widgets.
10. `ChooseModeProvider::Text()` and `ChooseModeProvider::Mat()` return meaningful label tokens and non-null mats for expected choose-mode slots.
11. `UIListDir::DrawWidgets()` and `UIListSlot::Draw()` are both reached for the choose-mode list with sane per-element positions and indices.

### Confirmed but easy to over-interpret

1. A `UIList` resource named `HamList.lst` logged `mListDir=(nil)` during one load path.
2. The overshell uses a shared `main` `PanelDir`, so top-level panel ownership is not a clean proxy for list visibility.
3. Frame capture showed lots of draws without obvious choose-mode mesh names.

None of those alone proves the final root cause.

## Missing Analysis From The Original Note

### Missing question 1: Which provider is actually bound at `choose_mode` entry?

The most important missing fact is the runtime answer to:

- Is the list bound to `ChooseModeProvider`?
- Is it bound to a `HamNavProvider` populated by DTA?
- Is it still using `HamNavList`'s fallback provider (`this`)?
- Is the provider set correctly, but never populated?

Until that is logged, the investigation is underspecified.

### Missing question 2: Do the DTA commands succeed, or merely dispatch?

The note proves `HandleType()` entered the `enter` handler and that 10 commands were encountered. That is not the same as proving the commands completed their intended side effects.

The next session needs per-command success/failure visibility for:

- object lookup failures
- null target objects
- property lookups that return null
- type coercion failures hidden behind `MILO_FAIL_DTA`
- commands that evaluate conditionals and skip later setup work

### Missing question 3: Is the list empty, or drawn off/invisibly?

These are different bugs:

- empty provider: `NumShowing() == 0`
- missing widgets: provider populated but `CreateElements()` never built widgets
- filled widgets but invisible: `FillElements()` ran, but composition/camera/pass state hid them

The original note leaned too early toward "resource dir load failure".

### Missing question 4: Is `ChooseModeProvider` even sensitive to Xbox-only services?

`ChooseModeProvider::UpdateList(bool)` hardcodes its mode symbols in C++. If `choose_mode` really uses `ChooseModeProvider`, then missing `content_mgr` or profile/DLC state is probably not the primary reason for a zero-count list. The more likely issue would be that `UpdateList()` never fires, the provider never gets bound, or the screen is actually using a different provider path.

### Missing question 5: What exactly does the screen DTA target?

The note says the `enter` handler contains:

- `hamprovider`
- `gamemode`
- `description.lbl`
- `game_mode_icon`
- `voice_input_panel`

What is still missing is the actual target object and message path for the list:

- Does DTA send `set_provider` to the `HamNavList` directly?
- Does it mutate a `HamNavProvider` via `append_nav_item`?
- Does it call `update_list` on `ChooseModeProvider`?
- Does it rely on `hamprovider` property changes to trigger a sink later?

That object/message graph should be captured explicitly in the next session.

## Ranked Hypotheses

### H1. Draw traversal / drawable registration failure

This is now the top hypothesis.

Possible forms:

- `right_hand.hnl` is not in the shared `main` `PanelDir` draw list
- `choose_mode.milo` content lives in a subdir/proxy that is not traversed by the active panel draw
- the object exists and refreshes, but is filtered before `DrawShowing()`
- `PanelDir`/`RndDir` draw registration was never rebuilt for this shared-dir path

Why this is strong:

- runtime logs already show a healthy provider/widget state
- provider payload is also healthy: labels and expected mats resolve correctly
- frame 500 still shows the panel drawing only 4 objects from `main`
- frame-500 draw dump still looks like shared overshell/background/helpbar content, not a clearly separate choose-mode list pass

### H2. Provider binding/population failure

This was the leading theory before the run. It is now mostly demoted.

Possible forms:

- DTA initially binds a `HamNavProvider` with 4 items
- later DTA swaps to `ChooseModeProvider` with 5 items
- if the list still renders wrong, the remaining provider angle is now secondary: wrong labels/materials, not empty population

Why this matters:

- it explains the item-count change from 4 to 5 during `choose_mode_panel.enter`
- it may still affect which labels/icons should appear, but it no longer explains "nothing renders"

### H3. DTA command success/failure mismatch

Likely contributing factor.

The screen may reach the right handler, but individual commands can still fail due to:

- missing named objects
- missing properties on `hamprovider`
- failed `FindObject`/`Obj<T>()` conversions
- missing `HamProvider` type data on a specific branch/run

### H4. Populated list, but element drawables are culled/hidden deeper in the widget path

This was underemphasized in the original note and matters because later native work already found cross-camera transparent composition bugs. If provider counts and `FillElements()` are healthy, the next session should pivot quickly into "drawn but hidden" instead of staying in DTA/resource land.

Concrete subtargets:

- `UIListMeshElement::Draw()`
- `UIListCustomElement::Draw()`
- `UILabel::DrawShowing()`
- material/pass state on `icon_*.mat` instances from `list_choose_mode.milo`

### H5. The note itself is stale against newer native work

Later native notes already describe:

- real `hamprovider` creation
- interactive choose-mode navigation
- rendering/composition fixes around transparent queues and camera switching

If the current branch already shows partial list/layout behavior, this note should be used as a historical blocker analysis, not as the current top-line truth.

## Concrete Instrumentation To Add

### 1. Provider binding trace

Add temporary logging to:

- `HamNavList::SetNavProvider()`
- `HamNavList::SetProvider()`
- `HamNavList::RealRefresh()`

Log:

- object name/path
- provider pointer and `ClassName()`
- `NumData()`
- `NumShowing()`
- whether `mListDirResource` is null
- `mListWidgets.size()`

This is the highest-signal instrumentation.

### 2. Resource dir resolution trace

Add temporary logging to:

- `ObjDirPtr::LoadFile()`
- `ObjDirPtr::PostLoad()`
- `HamNavList::PreLoad()` / `PostLoad()`
- optionally `UIList::PreLoadWithRev()` if the screen really instantiates nested `UIList`s

Log:

- requested file path
- whether a `DirLoader` was created/found/shared
- whether it resolves to a dir
- resolved dir class/name

Important: prove the exact failing resource path before assuming `HamList.lst` is the blocker.

### 3. DTA side-effect trace

Keep the existing `HandleType` trace, but add enough detail to show the command outcome:

- target object name
- message/command symbol
- whether the target object was found
- returned `DataNode` type/value where practical

Best places:

- `Object::HandleType()` tracing
- object lookup failure sites in the data/runtime path
- temporary `MILO_FAIL_DTA` breadcrumbs when the choose-mode path is active

### 4. Provider-population trace

If `choose_mode` uses `ChooseModeProvider`:

- log `ChooseModeProvider::UpdateList(bool)`
- log final `mModes.size()`

If `choose_mode` uses `HamNavProvider`:

- log `append_nav_item`
- log `mNavItems.size()`
- log labels as they are assigned

### 5. Widget population trace

Log once per refresh in:

- `HamNavList::Update()`
- `HamNavList::RealRefresh()`
- `UIListDir::CreateElements()`
- `UIListDir::FillElements()`

The key question is whether the system reaches "provider populated and widgets filled".

## Suggested Triage Order For The Next Session

### Step 1: Start from the new known-good population state

Already established by the 2026-03-10 run:

- list object = `right_hand.hnl (ui/choose_mode/choose_mode.milo)`
- final provider = `ChooseModeProvider`
- `NumData() = 5`
- `NumShowing() = 5`
- `widgets = 10`
- `listDir = list_choose_mode`

Do not spend the next session rediscovering that unless the branch changed.

### Step 2: Prove why the populated list never reaches draw

Immediate targets:

- verify whether `right_hand.hnl` is present in `choose_mode_panel`'s active drawable list
- inspect the `main` `PanelDir` draw list contents at frame 500
- inspect whether `choose_mode.milo` lives in a subdir/proxy not traversed by `PanelDir::DrawShowing()`
- verify whether `Showing()` or another filter excludes `right_hand.hnl`

### Step 3: Only if draw traversal looks correct, fall back to renderer/composition

If `HamNavList::DrawShowing()` is actually reached and the list is still invisible, then pivot back into render state, transparency, camera, or alpha/visibility logic.

### Step 4: Resource-dir work is now tertiary

The `list_choose_mode.milo` load path is already proven healthy on this branch. Do not treat `ResourceDirPtr` as the lead until a new regression disproves that.

## Fast Sanity Checks

### Sanity check A: ark presence

Verify the exact list/ribbon resource files exist in the asset source the native app is using. Do not only search for `HamList.lst`; search every resource path logged by `HamNavList::PreLoad()`.

### Sanity check B: branch drift

Compare this note against current `docs/native/NATIVE_PORT_STATUS.md` and the working tree before starting. If the branch already has choose-mode interaction or partial menu rendering, assume this note is stale in places.

### Sanity check C: avoid false negatives from mesh names

Use provider/widget traces as primary evidence. Use frame capture only as supporting evidence.

## Recommended Output To Leave For The Following Session

The next session should leave behind a small fact table, not another broad narrative:

1. list object name/path
2. live provider class
3. provider count at screen entry
4. whether `ChooseModeProvider::UpdateList()` ran
5. whether `HamNavProvider` got any nav items
6. whether `mListDirResource` resolved
7. whether `CreateElements()` and `FillElements()` ran
8. whether the populated choose-mode list is actually present in the active panel draw traversal
9. whether the failure ended up in draw traversal or later render/composition

If those eight answers are captured, the next session should be able to move directly from diagnosis to fix.

## Files Most Likely To Matter

- `src/system/hamobj/HamNavList.cpp`
- `src/system/hamobj/HamNavProvider.cpp`
- `src/lazer/meta_ham/ChooseModeProvider.cpp`
- `src/system/hamobj/Ham.cpp`
- `src/system/ui/ResourceDirPtr.h`
- `src/system/ui/ResourceDirPtr.cpp`
- `src/system/obj/Dir.h`
- `src/system/obj/DirLoader.cpp`
- `src/system/ui/UIListDir.cpp`
- `src/system/obj/Object.cpp`
- `docs/native/DTA_LOADING_BLOCKER.md`
- `docs/native/NATIVE_PORT_STATUS.md`

## Related Sessions

- Session 37: `HamNavList` element creation timing fix
- Session 39: DTA confirmed executing on native
- Session 40: typed/native `hamprovider` and interactive navigation baseline
- Session 41+: render/composition debugging means "drawn but visually wrong" is now a real competing explanation
