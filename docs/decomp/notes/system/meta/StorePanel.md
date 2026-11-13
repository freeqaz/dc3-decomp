# Lane notes: `src/system/meta/StorePanel.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@StorePanel@@UAAXXZ`

`StorePanel::Poll` -- moved verbatim from `src/system/meta/StorePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 116-122):

```text
// w20-f: the image's three local statics sit at scope indices 30/65/69
// (?BO@/?EB@/?EF@, map atexit names); the do/while MILO_ASSERT spelling gave
// 34/70/74.  Two MILO_ASSERT_IF (3 scopes each, see
// docs/decomp/patterns/fixable-scope-index.md) plus the `else if` for the
// single-item branch reproduce all three; the instructions are unchanged.
// Which -1 construct the original used in the second span is not
// determined by the indices alone.
```


## `?UpdateOffers@StorePanel@@MAA?AW4StoreError@@ABV?$list@UEnumProduct@@V?$StlNodeAlloc@UEnumProduct@@@stlpmtx_std@@@stlpmtx_std@@_N@Z`

`StorePanel::UpdateOffers` -- moved verbatim from `src/system/meta/StorePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 542-550):

```text
            // w22-ng: the image's call at 82E141FC..82E14218 is `lwz r11,
            // 0x70(vptr)` / bctrl with r4 = offer, r5 = &node->_M_data -- slot
            // 0x70 of ??_7StorePanel@@6BUIPanel@@@ is UpdateFromEnumProduct
            // (the slot after UpdateOffers, before the 0x74 _purecall).  Named
            // so native makes the same call: it used to be a raw (*vptr)[0x70/4]
            // that native skipped outright because LP64 shifts the offsets,
            // which left every enumerated offer's price/purchased/available
            // unset.  StoreOffer derives singly from StorePurchaseable, so the
            // upcast is a no-op (`mr r4, r31`, no null test).
```


## `?ValidateOffers@StorePanel@@IAAXAAV?$vector@PAVStoreOffer@@V?$StlNodeAlloc@PAVStoreOffer@@@stlpmtx_std@@@stlpmtx_std@@@Z`

`StorePanel::ValidateOffers` -- moved verbatim from `src/system/meta/StorePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 622-631):

```text
// w16-d (99.968, 6 offset rows): the image gives the second loop's cur_type
// slot 0x60 and songName its own 0x68 (OfferType temp 0x6c); we put cur_type
// on 0x64 and songName on 0x60 (temp 0x68). Folding songName into the
// MakeString call as a temp is worse (99.7).
// w24-slot (99.968, unchanged): image pools songOffer with cur_type (0x60) and
// gives pushName / songName their own words (0x64 / 0x68); ours pools
// songOffer with songName and pushName with cur_type.  The greedy
// most-recent-slot reuse of the slot rule cannot produce the image's pairing
// from these scopes unless cur_type sorts ahead of pushName, i.e. carries
// more IL references than its listing shows.  No source change tried.
```


## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/meta/StorePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

File-level (was lines 791-808; no definition follows it):

```text
// w8-j 2026-09-15 -- FLOOR at 99.950% for
// ?OnMsg@StorePanel@@IAA?AVDataNode@@ABVSigninChangedMsg@@@Z (168 B, 30 of 42
// instructions equal).  Two things, neither a source defect:
//  1. A callee-saved permutation.  The image assigns r27 to the hidden
//     return-slot pointer, r29 to msg.mData and r30 to the Profile; we assign
//     r30, r27 and r29 to the same three.  11 of the 12 rows are that rename.
//  2. The DataNode return is stored in the opposite order: the image writes
//     mValue (0x0) then mType (0x4), we write mType then mValue.  Our source is
//     already `return 1;` through DataNode(int), whose body in obj/Data.h:86
//     assigns mValue.integer first and mType second -- i.e. the SOURCE order
//     already matches the image and MSVC reordered the two independent stores
//     on our side.  Changing the ctor would reshape every DataNode construction
//     in the binary, so it is not a lever for this row.
// Also note idx 23: the image's callee is spelled
// ?Parent@Node@?$ObjPtrList@VCharLookAt@@VObjectDir@@@@UBAPAVObjRefOwner@@XZ
// where ours is ?GetPadNum@Profile@@QBAHXZ.  Both are a single `lwz` + `blr`,
// so this is an ICF fold that build/373307D9/icf_aliases.map does not yet group;
// it is a naming artifact, NOT a wrong call.
```
