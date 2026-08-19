# MSVC slots overloaded virtuals as one name-group, at the name's *first* declaration

**Status:** measured, 2026-08-19, with the project's own compiler
(`build/compilers/X360/16.00.11886.00/cl.exe`, `/GR /O1 /EHsc`).
**Runnable cases:** [`../experiments/msvc-vtable-overload-grouping/`](../experiments/msvc-vtable-overload-grouping/)
(`t1.cpp`, `t2.cpp`, `t3.cpp` + `README.txt`).

## The rule

> MSVC assigns vtable slots for **all virtual member functions sharing a name** as one
> **group**, positioned at that **name's first declaration in the class** — not at the
> declaration position of each individual overload.
>
> Concretely: an override of an inherited virtual keeps its inherited slot, as always.
> But a **new** virtual that *overloads a name the class also overrides* does not get its
> slot where it is written; it gets a slot immediately after the class's first mention of
> that name.

A single redundant `virtual void F(...) {}` override — one that adds nothing, because its
body is identical to the base's — is therefore **not** free. It changes where every new
overload of `F` lands, and shifts every new virtual declared after that point.

## What this replaces

An earlier phrasing of this finding said MSVC "hoists a new virtual to the **front** of
the derived new-virtual block when it overloads a name the class also overrides." That is
**only the special case** where the override happens to precede every other new virtual.
`t3.cpp` disproves the general form: with the override declared in the *middle*, the new
overload lands in the middle too. Do not reason from "front".

## The three cases

Base is the same in all three:

```cpp
class Base { public:
    virtual ~Base(); virtual void A(); virtual void SetADSR(int, const ADSR &); virtual void B(); };
```

| Case | Derived declaration order (new virtuals in **bold**) | Resulting new-virtual block |
|------|------------------------------------------------------|-----------------------------|
| `t1` | `SetADSR(ADSR)` override **first**, then **NewOne**, **SetADSR(ADSRImpl)**, **NewTwo** | `[SetADSR(ADSRImpl), NewOne, NewTwo]` |
| `t2` | **control** — same, override deleted: **NewOne**, **SetADSR(ADSRImpl)**, **NewTwo** | `[NewOne, SetADSR(ADSRImpl), NewTwo]` |
| `t3` | **NewOne**, then `SetADSR(ADSR)` override, then **NewTwo**, **SetADSR(ADSRImpl)**, **NewThree** | `[NewOne, SetADSR(ADSRImpl), NewTwo, NewThree]` |

Measured `??_7D1@@6B@` for `t3` — note `SetADSR(ADSRImpl)`, written **last** in the class,
takes the slot right after `NewOne`, i.e. the position of the *override*:

```
??_7D1@@6B@ DD  ??_R4D1@@6B@
        DD      ??_ED1@@UAAPAXI@Z
        DD      ?A@D1@@UAAXXZ
        DD      ?SetADSR@D1@@UAAXHABUADSR@@@Z      ; override -> inherited slot
        DD      ?B@D1@@UAAXXZ
        DD      ?NewOne@D1@@UAAXXZ
        DD      ?SetADSR@D1@@UAAXHABUADSRImpl@@@Z  ; new overload, declared LAST
        DD      ?NewTwo@D1@@UAAXXZ
        DD      ?NewThree@D1@@UAAXXZ
```

`t2`, with no override to anchor the name, puts `SetADSR(ADSRImpl)` in plain declaration
order. That control is what makes the reading unambiguous.

To reproduce:

```sh
for t in t1 t2 t3; do
  wibo build/compilers/X360/16.00.11886.00/cl.exe /nologo /c /GR /O1 /EHsc /TP \
       /FAsc /Fa$t.asm /Fo$t.obj $t.cpp
  awk '/^\?\?_7D1@@6B@ DD/{f=1} f{print; if(/^$/) exit}' $t.asm
done
```

## The DC3 instance

`StandardStream` (`src/system/synth/StandardStream.h`) declared

```cpp
virtual void SetADSR(int, const ADSR &) {}   // identical to Stream's own empty body
```

`ham_xbox_r.map` has **no** `?SetADSR@StandardStream@@UAAXHABVADSR@@@Z`, so retail never
had that declaration. It was invisible on every ordinary ruler — the override ICF-folds
into the same `OnlyReturns` stub as the base — but it dragged
`SetADSR(int, const ADSRImpl &)` up to slot `+0xc8`, which the target gives to
`GetChannel`, and pushed every slot below it four bytes out of place. Dropping the
redundant override (and making `GetChannel` virtual, which it also had to be) put every
slot `0x00`–`0xe0` back in agreement and took
`?GetChannel@StandardStream@@UBAPAVStreamReceiver@@H@Z` from **0 % to 100 %**.

## When to suspect it

Whenever a class **redeclares an inherited virtual that it also overloads**, and the
target's vtable disagrees with yours from some slot onward by a constant shift. The tell
is a *run* of slots off by one entry starting partway down — not a single wrong slot.
Grep candidates with:

```sh
# classes that both override and overload the same virtual name
grep -rn "virtual .* \bNAME\b" src/ | ...
```

and confirm against the target with the `vtable` skill (dumps slot -> symbol from the
original COFF objects) or `data-diff` on `??_7Class@@6B@`.

Two properties make this class of bug worth hunting:

- **It is metric-invisible at the source of the error.** The redundant override compiles
  to a body that ICF-folds away, so no function-level percentage moves because of it. What
  moves is the *vtable data symbol* and the rows of every function that got the wrong slot.
- **It is behaviourally live.** A shifted slot means virtual dispatch calls the wrong
  function at runtime, on both Xbox and native.
