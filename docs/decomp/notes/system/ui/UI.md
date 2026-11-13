# Lane notes: `src/system/ui/UI.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/ui/UI.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

File-level (was lines 16-22; nearest following definition: `DebugUIFlow`):

```text
// w8-e 2026-09-15: fn_8277AFC0 (132 B, 0%) is ?TerminateCallback@@YAXXZ, a
// file-static.  ham_xbox_r.map lists that name TWICE -- 8265EA60 from
// rndobj:Rnd.obj (size 0x38) and 8277AFC0 from ui:UI.obj (this one) -- both bare
// `f`.  symbols.txt can carry one symbol per name and line 144853 binds the
// rndobj copy, so dtk parks this one as fn_8277AFC0.  The row can never pair and
// can never score; adding a symbols.txt line here would merely park Rnd's copy
// instead (zero-sum).  Measured 0.0%, structurally unscoreable.
```

File-level (was lines 1297-1309; no definition follows it):

```text
// w8-j 2026-09-15: fn_8277AFC0 (132 B) is unscoreable from this TU, and it is
// not a missing body.  `?TerminateCallback@@YAXXZ` is a file-static registered
// with atexit-style teardown, so ham_xbox_r.map prints that ONE mangled name at
// TWO addresses with a bare `f` flag (internal linkage in two TUs):
//     8265ea60  f  rndobj:Rnd.obj   56 B
//     8277afc0  f  ui:UI.obj       132 B   <- this row
// config/373307D9/symbols.txt:144853 binds the name to 0x8265EA60, and dtk can
// only name one address per symbol, so the other is carved as fn_<addr> and can
// never pair against anything our object emits.  The binding that exists is the
// one currently SCORING: rndobj:Rnd's 56 B copy reads 100.0.  Rebinding to
// 0x8277AFC0 would be zero-sum-plus-a-goalpost-move (+76 B gross, but it trades
// a scoring row for an fn_ row and the map names neither as the "wrong" one).
// Measured 0.0%, refuted as a rename target, left as-is deliberately.
```


## `?Draw@UIManager@@UAAXXZ`

`UIManager::Draw` -- moved verbatim from `src/system/ui/UI.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 251-259):

```text
    // w23-g10: the image's Draw selects no camera or environment -- each
    // PanelDir::DrawShowing selects its CamOverride() (its own cam, else
    // TheUI->GetCam()) and TheUI->GetEnv(), and restores the caller's cam.
    // Native used to pre-select [ui.cam] + the UI env here and restore after;
    // removing that made no visible difference over a boot -> gameplay
    // screenshot tour (the residual pixel diffs are the animated album-art /
    // background textures, which differ run to run on the same binary too).
    // What remains is the env-gated MILO_UI_CAM_MODE diagnostic, which only
    // mutates [ui.cam]; the default (original) leaves it untouched.
```


## `?IsGameScreenActive@UIManager@@QAA_NXZ`

`UIManager::IsGameScreenActive` -- moved verbatim from `src/system/ui/UI.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 397-425):

```text
    // NOTE (w7-av): BEHAVIOURAL FIX.  We used to write `ret &= cur != BottomScreen()`,
    // which is the opposite of the image.  The image's tail is
    //   0x8277B674 subic r11, r11, 0x1 / 0x8277B678 subfe r11, r11, r11
    //   / 0x8277B67C and r9, r11, r9
    // i.e. CA-1, a -1/0 mask that is all-ones exactly when the difference is ZERO,
    // so `ret` survives only when mCurrentScreen == BottomScreen().
    // w13-d (95.0 -> 97.5): the conditional clear below is what emits that
    // mask (the `ret &= cur == BottomScreen()` spelling gave cntlzw/extrwi).  It
    // only does so once the null test is SIGNED, as the image's is (`cmpwi cr6,
    // r11, 0x0` at 0x8277B650) -- with an unsigned `cur != nullptr` the same
    // clear if-converts differently (89.2, w7-av).  Remaining 2 rows: the image
    // puts that compare in cr6, we in cr0; `if ((int)cur)`, `!((int)cur == 0)`
    // and `(int)cur != 0 && cur != BottomScreen()` all keep cr0, and reading
    // mCurrentScreen twice instead of through `cur` gets cr6 but loses the
    // separate return block (89.8).
    // w21-s (97.5, same 2 rows): behaviour re-checked vs 0x8277B5C8.. (null
    // BottomScreen -> false; mask keeps ret only when cur == BottomScreen();
    // null cur returns ret unchanged).  Also measured, none closes the cr6:
    // `(int)cur > 0 || (int)cur < 0` (gets cmpwi cr6 but adds a test, 93.3),
    // `!= (int)nullptr`, `!= zero` local, `(long)`, `(bool)cur`, switch on
    // (int)cur, `!((int)cur == 0)`, a named bottom local, operand-swapped
    // compare, int-cast or subtract-form inner compare, ternary select (all
    // 97.5); `ret &= cur ? ... : true` (86.7), `!cur || cur == Bottom` (84.1 /
    // 87.5), early return on null cur (93.3), cur loaded first (93.3),
    // `int ret` (92.1), `int cur` (89.1).
    // w21-ay (97.5 -> 100): the image's signed `cmpwi cr6` null test, the
    // reused value in the BottomScreen() fallback (`mr r10, r11`) and the
    // separate `mr r3, r9` return block all come from testing the inline
    // CurrentScreen() accessor twice (no named local).  Same behaviour as before.
```


## `?Poll@UIManager@@UAAXXZ`

`UIManager::Poll` -- moved verbatim from `src/system/ui/UI.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 706-714):

```text
                        // This skip stands in for title_screen's NAV_SELECT_MSG on
                        // title_screen_menu (ui/title/title.dta), which sets
                        // $post_load_dest_screen to main_screen before going to
                        // wait_main_after_saveload_screen; that screen's
                        // saveload_complete then does
                        // {ui goto_screen $post_load_dest_screen}.  Skipping
                        // without it handed OnGotoScreen a null screen, which only
                        // the (now removed, w23-g10) native main_screen fallback
                        // papered over.
```

Inside the body (was lines 739-743):

```text
            // w23-g10: evaluate the probes only when they will be printed.  Run
            // unconditionally they called Exiting() (a DTA `exiting` HandleType
            // per panel) and IsBlockingTransition() ahead of -- and, unlike --
            // the image's short-circuited condition below on every new
            // transition.
```
