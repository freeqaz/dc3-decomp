# Lane notes: `src/lazer/meta_ham/HamStoreProvider.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?PopulateOffersInCart@HamStoreProvider@@AAAXXZ`

`HamStoreProvider::PopulateOffersInCart` -- moved verbatim from `src/lazer/meta_ham/HamStoreProvider.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 291-315):

```text
// w8-l: 99.955360 normalized, the only function keeping this unit from 100%
// (63/64).  Exactly two rows, [43] and [44], and they are the same two `lis`
// instructions with their order exchanged -- destination registers and values
// agree on both sides:
//   target  lis r23, ?TheNetCacheMgr@@3PAVNetCacheMgr@@A@h
//           lis r27, lbl_8311B160@h          (this function's static guard)
//   ours    lis r27, ?$S8@?O@??PopulateOffersInCart@...@4IA@h
//           lis r23, ?TheNetCacheMgr@@3PAVNetCacheMgr@@A@h
// Nothing is computed differently; MSVC schedules the two independent address
// loads in the opposite order.  Same shape as MiniGameMgr::UpdateCascadeMovePool
// [43]/[44] and Curl_base64_encode [40]/[43] in this wave -- no source lever
// found for it in any of the three.
// w16-e: `if (!IsAvailable() && !IsDebug()) continue;` before the static is
// inert (same two lis rows).
// w21-ar (99.955, same rows): reading the member `offer->isAvailable`
// directly (one inline level fewer, as RefreshOffers spells it) is inert.
// w21-bn (99.955, same two `lis` rows).  Standalone cl.exe probe reproduces
// our order (guard `lis` before TheNetCacheMgr's).  Inert there: the test
// hoisted into a `bool ok`, `== false && == false) continue;`, `!= 0` on
// IsDebug, the raw member, an earlier/later function in the TU that also
// references TheNetCacheMgr.  The ONLY spelling that flips the two `lis` is
// declaring `static Symbol song` ABOVE the IsAvailable/IsDebug test -- which
// moves the guard check ahead of the test (the image checks it after
// IsDebug: 8297FC78 `lwz r11, lbl_8311B160` follows the 8297FC6C IsDebug
// call), so it is a different function.  Not shipped.
```
