# Lane notes: `src/lazer/meta_ham/HamStorePanel.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?UpdateOffers@HamStorePanel@@MAA?AW4StoreError@@ABV?$list@UEnumProduct@@V?$StlNodeAlloc@UEnumProduct@@@stlpmtx_std@@@stlpmtx_std@@_N@Z`

`HamStorePanel::UpdateOffers` -- moved verbatim from `src/lazer/meta_ham/HamStorePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 539-550):

```text
    // w26-f2 (fuzzy 99.453125, canonical 100.0): the two charged rows are the
    // tie-0 code band -- image holds `check` in r10 and `it->mOfferID` in r9,
    // we hold them the other way round (`check` r9, mOfferID r10).
    // Tap: four priority-16 tie-0 mints here, CFREE=4 with CFREE-ben=1, so
    // C-F=3 and the pop order (reverse mint) gives r11=sym19, r10=mOfferID,
    // r9=check, r8=sym16.  The image's order is what you get if all four are
    // recycled (F>=4): r11=sym16, r10=check, r9=mOfferID, r8=sym19.  So the
    // lever is one more free in that band, per C2RS-BRIDGE 8.4.
    // Tried and measured: a named `int purch = listIt->mPurchased` -- the 8.6
    // candidate -- produced NO new CDEMOTE in the tap (c1 folds the copy), so
    // that lever does not engage on this row; swapping the `==` operands is
    // 99.375 (normalized stays 100.0, so that one was reverted as worse fuzzy).
```


## `?OnMsg@HamStorePanel@@IAA?AVDataNode@@ABVRCJobCompleteMsg@@@Z`

`HamStorePanel::OnMsg` -- moved verbatim from `src/lazer/meta_ham/HamStorePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 642-648):

```text
            // The image errors out here too: 0x82934EB4 `b 0x82934D80` joins
            // the remove-failure tail, which prints and then calls
            // ExitError(3) at 0x82934DA8 (`this` stays live in r30/r31 for
            // it).  Ours only logged (w20-a).  The early `return 1` is
            // behaviour-neutral (the chain falls to `return 1` anyway); it
            // makes MSVC keep the mJobs[1] copy of the shared tail, as the
            // image does, instead of this one.
```
