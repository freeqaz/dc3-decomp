# Dropped static initializers: the class objdiff cannot score

**dc3-decomp (title 373307D9).** A static whose declaration lost its initializer
lands in our `.obj`'s `.bss` and reads zero at runtime, while the shipped image
defines it in `.data`/`.rdata` with real content. The instruction streams on both
sides are identical, so **objdiff scores these at 100% and always will** — it
scores code and never asks what a static's initial bytes were.

Every fix in this class is a pure behaviour fix with a guaranteed **zero** change
to the match percentage. Do not judge the work by the metric.

## Finding them

```sh
python3 scripts/analysis/bss_initializer_scan.py
```

A symbol we place in a section flagged `IMAGE_SCN_CNT_UNINITIALIZED_DATA` whose
target counterpart holds **nonzero** bytes is a dropped initializer.

The discriminator has to be **content**, not section name. The target objects
under `build/373307D9/obj/` are dtk splits of the shipped image, so *every*
section carries raw bytes — including the one literally named `.bss`. Matching on
the name finds nothing.

## The census: exactly ten, all fixed

| symbol | ours | target | what the wrong value did |
|---|---|---|---|
| `UIComponent::sSelectFrames` | 0 | 10 | |
| `RndMesh::sLastCollide` | 0 | −1 | |
| `gPendingResponse` | `kVersion` | `kInvalidOpcode` | |
| `gEvent`, `gVoiceThread` | `NULL` | `INVALID_HANDLE_VALUE` | |
| `Voice::sHeadsetTarget` | 0 | −1 | |
| `gTempPortraitOffset` | 0.0f | 0.125f | |
| wordwrap `g_uOption` | 0 | 1 | |
| `CharClipDisplay::sZoom` | 0.0f | 1.0f | `16.0f / sZoom` returned ±inf |
| `DxRnd::CopyPostProcess::sCopyPostInited` | false | **true** | guarded block never ran (see below) |
| `TheLocale.mInitialized` (+0x1c) | 0 | **1** | skipped the entire locale load |
| `CSampleXAPOBase<SynapseAPO,…>::m_regProps` | all zero | full struct | APO registered with a null CLSID |

> ⚠ **CORRECTED 2026-09-16 — "ten is the whole binary" was never measured, and
> two more instances walked straight through it.** The sentence that stood here
> read: *"Ten is the whole binary. The scan returns zero hits as of 2026-08-19;
> if it ever returns more, someone added a declaration without its initializer."*
> Both halves are wrong.
>
> The scan joined **by symbol name inside a matched object pair**
> (`bss_initializer_scan.py`, `if name not in tgt: continue`). Its coverage
> block at the time read:
>
> ```
> universe : 16238  (defined symbols WE place in .bss, across paired objects)
> examined :  3295  (3295/16238 = 20.29%)
> dropped  : 12943
>     no-name-match-in-paired-target : 12939
>     capped-by-max-size             :     4
> ```
>
> **Four fifths of the population was discarded by an uncounted `continue`**,
> under a summary line whose denominator (`scanned 980 object pairs`) counts
> *object pairs* and not *symbols* — so nothing in the output could contradict
> it. A census of 20% was presented as the whole binary.
>
> ⚠ An earlier draft of this correction said *30,548 / 3,578 / 11.7%*. Those
> came from an ad-hoc count taken before the instrument was fixed, and they are
> **record** counts, not **name** counts: COFF emits several symbol records per
> name, the scan dedups with `setdefault`, and 30,548 is exactly the un-deduped
> record total over the same 980 paired objects (16,252 names over all 990 of
> our objects; 10 have no target counterpart). Quote the coverage block, which
> is reproducible by running the thing.
>
> Two real instances landed **after** that exhaustion verdict, neither findable
> by this scan:
>
> * **`Game::Poll::sLastBeat`** (`88c3d9c20c`) — we declared `static float
>   sLastBeat;` (`.bss` zero); the image holds **−1** in writable `.data`. dtk
>   names a function-local static `lbl_<addr>` on the target side, so there is
>   **no name to join on**. Found by `mutable_float_audit.py` as `lbl_82F1A524`,
>   not by this scan. `sLastBeat` is process-lifetime and never reset per song,
>   so the first Poll's big-jump window was (−4, 4) instead of (−3, 5),
>   desynchronising every beat-scheduled task for songs whose first polled beat
>   fell in the gap.
> * **`GainEffect::sGain`** (`df13adcd1c`) — the target side *is* named
>   (`?sGain@GainEffect@@0MA`), but our definition sat in the **wrong TU**
>   (`Mic.cpp`), and this scan pairs object-by-object, so the two never met.
>
> ✅ **Both blind spots CLOSED 2026-09-16.** The scan now runs three joins and
> its coverage block on the same tree reads:
>
> ```
> universe : 16238  (defined symbols WE place in .bss, across paired objects)
> examined : 15464  (15464/16238 = 95.23%)
> dropped  :   774
>     coff-section-symbol-not-a-variable             : 697
>     aligned-instruction-opcode-differs             :  38
>     no-code-reference-in-our-object                :  13
>     capped-by-max-size                             :  10
>     referencing-function-absent-from-paired-target :   8
>     reference-site-unalignable                     :   4
>     address-join-ambiguous                         :   2
>     target-relocation-is-not-the-low-half          :   2
> ```
>
> **20.29% → 95.23%**, same denominator, same numerator definition — the tool
> reproduces the old figure exactly under
> `--no-address-join --no-cross-tu` (3294/16238 = 20.29%), so the before/after
> is one instrument measuring itself, not two instruments being compared.
> 697 of the 16,238 universe rows turn out to be COFF **section-definition**
> symbols (the record literally named `.bss`) rather than variables; they are
> kept in the universe so the denominator stays comparable and dropped rather
> than examined. Against the 15,541 real variables, coverage is **99.50%**.
>
> ⚠ **The map-based address join named above does not work, and this is
> measured, not argued.** Of the 12,473 distinct names we place in `.bss`,
> 2,297 appear in `ham_xbox_r.map`; of the 8,893 the name join cannot resolve,
> **0** appear in it. The unresolvable population is almost entirely
> function-local statics, and MSVC mangles those with a **per-TU scope
> ordinal** (`?1@`, `?6@`, `?CH@`) that counts scopes as the compiler walks the
> file — so ours and the image's differ whenever anything earlier in the TU
> differs. This is the same `?BD@` vs `?BH@` divergence CLAUDE.md documents as
> a relocation-name noise class the graded ruler deliberately exempts. **A
> function-local static is unjoinable by name on principle**, whether the name
> comes from a target object or from the map.
>
> What locates it is the **code that reads it**: our object has a `REFLO`
> relocation inside function F naming our `.bss` symbol; the target's F, at the
> aligned offset, names whatever the image put there — usually `lbl_<addr>`, a
> name that *is* an address. Alignment compares instruction words with only the
> *relocated field* masked (not objdiff's `funclet_signature`, which zeroes the
> whole word and would let a `lfs` align to a `lwz`), falls back to `difflib`
> when the streams differ, and then requires the target instruction to carry a
> `REFLO` of its own and to agree in its top 16 bits. The `difflib` tier is
> load-bearing rather than a courtesy: `Game::Poll` is 98.77% and its masked
> stream is *not* identical, so `sLastBeat` is reachable only through it.

## Open: the two the widened scan found immediately

Both were invisible to the name-only join, and both are in the population it
could not reach. Neither is fixed yet.

| symbol | ours | target | reached via |
|---|---|---|---|
| `g_LineBreakTable` (`wordwrap.cpp`) | 146 entries of zero in `.bss` | `lbl_82F16AD0`, `.data`, 0x244 B of populated table | address join (aligned) |
| `gPollToken` (`Loader.cpp`) | `static int gPollToken;` → 0 | `lbl_82F189F8` = `.4byte 0x00000001` | address join (exact) |

`g_LineBreakTable` is the more serious of the two. `wordwrap.cpp` runs three
binary searches over it (`CantStartLine`, `CantEndLine`), and the image's table
is a sorted list of 145 `{wchar_t ch; u8 cantBreakBefore; u8 cantBreakAfter;}`
records — `0x00210100` is `'!'` with *cannot break before*, `0x00280001` is
`'('` with *cannot break after*, running up through `0xFFE6`. Ours is all
zeroes, so every search misses, both helpers always return false, and line
breaking never suppresses a break before `!`, `)`, `?`, `:` or any CJK
punctuation. Note our declaration is `[146]` against the image's 0x244 = 580
bytes = 145 entries; settle the count against the listing when restoring it.

`gPollToken`'s source comment already *names* `lbl_82F189F8` as its target
symbol — someone identified the address and never read the value out of it,
which is exactly the failure mode the "uninitialized, matches anyway" trap
below describes.

## Two traps this class sets

**A guard flag that starts `true` is not a latch.** `sCopyPostInited` reads

```cpp
static bool sCopyPostInited = true;
if (sCopyPostInited) {
    sCopyPostInited = true;   // redundant store, not a one-shot
    ...
}
```

Started at `true` the body runs on *every* call; started at `false` it is dead
code. Both compile to the same instructions — `lbz` / `cmplwi` / `beq`, then
`stb r29` inside the taken arm — so nothing but the data bytes distinguishes
"runs always" from "never runs". Read the initial value out of the image before
you reason about a flag's meaning.

**"Uninitialized, matches the original anyway" is usually a missing initializer.**
`Locale::Init` gates the whole locale load on `mInitialized`, and the source
carried a comment asserting that member was an uninitialized read the original
got away with. It is not: the image holds `0x01` at `TheLocale+0x1c`. Any comment
in this codebase claiming a global "happens to be nonzero" is a scan hit that was
rationalised instead of measured.

## Restoring an initializer without moving code

For a scalar, adding `= value` needs no guard and cannot move an instruction.

For a member of a global with a constructor, put it in the member-init list:
MSVC folds the constant into `.data` and leaves only the non-constant stores in
`??__E<name>`, so the dynamic initializer's instruction stream is unchanged.
`Locale() : mInitialized(true) {}` kept `??__ETheLocale` at 8 instructions, all
equal.

Verify all three of: the symbol moved out of `.bss`, its bytes equal the target's,
and `run_objdiff` reports the same mismatch count as before. A rendered "100.0%"
is not byte-identity — compare counts.

## Open: MSVC's static/dynamic split for large aggregates

`m_regProps` (0x42c bytes) is the one place the values now match but the *layout*
does not. The original folds only the leading 0x24 bytes (clsid + the nine chars
of `L"SampleAPO"`) into `.data` and emits a 144-byte
`??__E?m_regProps@…@YAXXZ` that memsets the `FriendlyName` tail, memcpys the
pooled `??_C@_1FA@MJNECBMC@` copyright literal into `CopyrightInfo`, memsets that
tail, and stores the seven trailing `UINT`s.

Our `cl.exe` — the same binary, the same `/O1 /Oi /EHsc /TP` — folds all 0x42c
bytes and emits no dynamic initializer. Falsified so far:

* **not a literal-length threshold** — a 160-character copyright string still folds;
* **not explicit-specialization vs primary-template definition** — both fold.

`src/link_glue.cpp` still `/ALTERNATENAME`s the absent `??__E?m_regProps` for this
and every sibling effect, so recovering the split is worth ~144 B per effect.
