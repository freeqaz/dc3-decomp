# A dead `AddRef(this)` branch makes a stack local "escape", and costs a destructor its fold

**Class:** fixable, header-level. **Measured:** wave 12 (2026-10-02), lane w12-o, on
`ObjPtrVec<T, ObjectDir>::operator=` — **13 instantiations, 87.838 → 100.0, all 74
instructions equal**, three units completed outright, 0 regressions binary-wide (whole-binary
row diff after a full `ninja`, since the header is PCH-reached).

## Symptom

A destructor on a stack temporary keeps a null test plus cleanup that the image does not
have. On `ObjPtrVec::operator=` it was a **9-instruction null test + ring unlink** in the
per-iteration `~Node` (old rows 69-77), with a register rotation riding on it. The image
destroys the node with only the base-vptr reset: the compiler had *proved* `mObject == 0`.

The tell that this is a proof problem rather than a codegen floor: adding an explicit
`mObject = 0` store just before destruction scored **98.65** — the null test vanished, but
at the price of a store the image does not have. When an extra assignment removes a test,
look for the source shape that lets the compiler know the value without the assignment.

## Mechanism

**Observed** with the real X360 `cl.exe /O1` on scratch TUs (`S s; s.a = 0; f(s); if (s.a) h();`,
counting whether `bl h` survives):

| callee `f(const S&)` does | null test |
|---|---|
| only reads `s` (`t2`) | **folded** |
| stores `&s` in a global, nothing later writes through it (`t4`) | **folded** |
| passes `&s` to an external, unseen function (`t5`) | **kept** |

So this compiler does intraprocedural-plus-TU-local escape analysis: a local whose address
reaches code it cannot see is assumed modifiable by every later call, and facts about its
fields stop propagating. Passing it by `const&` to a callee it can see is *not* an escape.

**Inferred from the fix, not observed directly:** `Node(owner)` constructed its base through
`ObjRefConcrete(T1 *obj)`, whose body is `if (mObject) mObject->AddRef(this);`. With a null
argument that branch is dead — but `AddRef` stores `this` into the referent's ref ring, so if
escape analysis runs before the dead branch is folded away, the node counts as escaped and
`push_back`/`Set` may "modify" it. Removing the AddRef path from the constructor is what made
`mObject == 0` survive to the destructor, which is consistent with that ordering.

## Lever

Construct the base through a path with no escaping call at all — here a protected default
constructor that only nulls the pointer:

```cpp
// obj/Object.h
template <class T1, class T2> class ObjRefConcrete : public ObjRef {
protected:
    T1 *mObject;
    ObjRefConcrete() : mObject(nullptr) {}   // no AddRef path
public:
    ObjRefConcrete(T1 *obj);                  // if (mObject) mObject->AddRef(this);
    ...
};
// ObjPtrVec::Node
Node(ObjRefOwner *owner) : mOwner(owner) {}   // was : ObjRefConcrete<T1>(nullptr), mOwner(owner)
```

Faithful by construction: for a null argument the old constructor did exactly `mObject =
nullptr` (its `AddRef` and native `RefAudit::Retarget` both sit inside `if (mObject)`), and the
`ObjRef` base is default-constructed either way, so native's self-loop still runs.

## Refuted on the same function (do not retry)

`const Node newNode(this)` (inert); `push_back(Node(this))` (85.09); a comma-expression
temporary; a non-inlined helper (52.6); `__forceinline` on the `ObjRefConcrete` and `Node`
ctors/dtors; `push_back` cut down to its copy-construct path; emptying the `ObjRefConcrete`
copy constructor. Every one leaves the null test, because none removes the escape.

## Where else to look

Any temporary of a type whose *constructor* can hand `this` to something unseen — ref-counted
handles, intrusive lists, observer registration — followed by a destructor or test that the
image folds and we do not. The scratch TUs are under `/home/free/tmp/w12o-snap/scratch_ipa/`
(`run.sh <tN>`).
