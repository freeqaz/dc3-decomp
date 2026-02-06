# DC3 Decomp Macro Reference

This document covers the macro system used throughout the DC3 decompilation project. These macros provide standardized patterns for common engine operations.

## Important Warning

**Macro usage is codegen-sensitive.** Many of these macros embed file paths, line numbers, or string literals directly into the compiled binary. Modifying them incorrectly will cause the assembly to not match the original.

Key rules:
- **Never modify `MILO_ASSERT()` string content** - the condition text is baked into the binary
- **`OBJ_MEM_OVERLOAD` line numbers must match exactly** - they're used for memory tracking
- **Handler/propsync ordering affects codegen** - don't reorder without verification

## Quick Reference Table

| Macro | Category | Source File | Brief Description |
|-------|----------|-------------|-------------------|
| `OBJ_CLASSNAME` | Object Lifecycle | `obj/Object.h` | Define class name methods |
| `NEW_OBJ` | Object Lifecycle | `obj/Object.h` | Define factory function |
| `REGISTER_OBJ_FACTORY` | Object Lifecycle | `obj/Object.h` | Register class with factory |
| `OBJ_SET_TYPE` | Object Lifecycle | `obj/Object.h` | Define SetType method |
| `OBJ_MEM_OVERLOAD` | Memory | `utl/MemMgr.h` | Memory operators for Hmx::Object |
| `MEM_OVERLOAD` | Memory | `utl/MemMgr.h` | Memory operators for non-Objects |
| `MEM_ARRAY_OVERLOAD` | Memory | `utl/MemMgr.h` | Array memory operators |
| `RELEASE` | Memory | `macros.h` | Delete and null pointer |
| `BEGIN_HANDLERS` | Message Handlers | `obj/Object.h` | Start handler block |
| `HANDLE` | Message Handlers | `obj/Object.h` | Handle message with function |
| `HANDLE_ACTION` | Message Handlers | `obj/Object.h` | Handle message with action |
| `HANDLE_EXPR` | Message Handlers | `obj/Object.h` | Handle message returning expression |
| `HANDLE_MESSAGE` | Message Handlers | `obj/Object.h` | Handle typed message |
| `HANDLE_SUPERCLASS` | Message Handlers | `obj/Object.h` | Forward to parent class |
| `END_HANDLERS` | Message Handlers | `obj/Object.h` | End handler block |
| `BEGIN_PROPSYNCS` | Property Sync | `obj/Object.h` | Start propsync block |
| `SYNC_PROP` | Property Sync | `obj/Object.h` | Sync property to member |
| `SYNC_PROP_SET` | Property Sync | `obj/Object.h` | Sync with custom setter |
| `SYNC_PROP_MODIFY` | Property Sync | `obj/Object.h` | Sync with modification callback |
| `SYNC_PROP_BITFIELD` | Property Sync | `obj/Object.h` | Sync bitfield property |
| `SYNC_SUPERCLASS` | Property Sync | `obj/Object.h` | Forward to parent class |
| `END_PROPSYNCS` | Property Sync | `obj/Object.h` | End propsync block |
| `BEGIN_SAVES` | Serialization | `obj/Object.h` | Start Save method |
| `SAVE_REVS` | Serialization | `obj/Object.h` | Write revision numbers |
| `SAVE_SUPERCLASS` | Serialization | `obj/Object.h` | Call parent Save |
| `END_SAVES` | Serialization | `obj/Object.h` | End Save method |
| `INIT_REVS` | Serialization | `obj/Object.h` | Define revision constants |
| `BEGIN_LOADS` | Serialization | `obj/Object.h` | Start Load method |
| `LOAD_REVS` | Serialization | `obj/Object.h` | Read revision numbers |
| `ASSERT_REVS` | Serialization | `obj/Object.h` | Validate revision numbers |
| `LOAD_SUPERCLASS` | Serialization | `obj/Object.h` | Call parent Load |
| `LOAD_BITFIELD` | Serialization | `obj/Object.h` | Load bitfield member |
| `END_LOADS` | Serialization | `obj/Object.h` | End Load method |
| `BEGIN_COPYS` | Object Copying | `obj/Object.h` | Start Copy method |
| `CREATE_COPY` | Object Copying | `obj/Object.h` | Cast source object |
| `BEGIN_COPYING_MEMBERS` | Object Copying | `obj/Object.h` | Start member copy block |
| `COPY_MEMBER` | Object Copying | `obj/Object.h` | Copy single member |
| `COPY_SUPERCLASS` | Object Copying | `obj/Object.h` | Call parent Copy |
| `END_COPYS` | Object Copying | `obj/Object.h` | End Copy method |
| `MILO_ASSERT` | Debug | `os/Debug.h` | Assert with line number |
| `MILO_ASSERT_FMT` | Debug | `os/Debug.h` | Assert with format string |
| `MILO_FAIL` | Debug | `os/Debug.h` | Fatal error |
| `MILO_WARN` | Debug | `os/Debug.h` | Warning message |
| `MILO_NOTIFY` | Debug | `os/Debug.h` | Notification message |
| `MILO_TRY`/`MILO_CATCH` | Debug | `os/Debug.h` | Exception handling |
| `FOREACH` | Iteration | `utl/Std.h` | Forward iteration |
| `FOREACH_CONST` | Iteration | `utl/Std.h` | Const forward iteration |
| `FOREACH_REVERSE` | Iteration | `utl/Std.h` | Reverse iteration |
| `DIM` | Utility | `macros.h` | Array dimension |

---

## 1. Object Lifecycle Macros

### OBJ_CLASSNAME

**Defined in:** `src/system/obj/Object.h`

**Purpose:** Defines the `ClassName()` and `StaticClassName()` methods for an Object class.

```cpp
#define OBJ_CLASSNAME(classname)                                                         \
    virtual Symbol ClassName() const { return StaticClassName(); }                       \
    static Symbol StaticClassName() {                                                    \
        static Symbol name(#classname);                                                  \
        return name;                                                                     \
    }
```

**Usage:**
```cpp
class BeatClock : public RndPollable {
public:
    OBJ_CLASSNAME(BeatClock);
    // ...
};
```

**Notes:** Place in the public section of your class declaration.

---

### NEW_OBJ

**Defined in:** `src/system/obj/Object.h`

**Purpose:** Defines the static factory function for creating new instances.

```cpp
#define NEW_OBJ(objType)                                                                 \
    static Hmx::Object *NewObject() { return new objType; }
```

**Usage:**
```cpp
class MyObject : public Hmx::Object {
public:
    NEW_OBJ(MyObject);
};
```

---

### REGISTER_OBJ_FACTORY

**Defined in:** `src/system/obj/Object.h`

**Purpose:** Registers a class with the object factory system.

```cpp
#define REGISTER_OBJ_FACTORY(objType)                                                    \
    Hmx::Object::RegisterFactory(objType::StaticClassName(), objType::NewObject);
```

**Usage:**
```cpp
void UIList::Init() {
    Register();
    REGISTER_OBJ_FACTORY(UIListArrow)
    REGISTER_OBJ_FACTORY(UIListDir)
    REGISTER_OBJ_FACTORY(UIListLabel)
    // ...
}
```

---

### OBJ_SET_TYPE

**Defined in:** `src/system/obj/Object.h`

**Purpose:** Defines the `SetType()` virtual method for type-based configuration.

```cpp
#define OBJ_SET_TYPE(classname)                                                           \
    virtual void SetType(Symbol classname) {                                              \
        DataArray *def;                                                                   \
        if (!classname.Null()) {                                                          \
            static DataArray *types =                                                     \
                SystemConfig("objects", StaticClassName(), "types");                      \
            DataArray *found = types->FindArray(classname, false);                        \
            if (found) {                                                                  \
                SetTypeDef(found);                                                        \
            } else {                                                                      \
                MILO_NOTIFY(                                                              \
                    "%s:%s couldn't find type %s", ClassName(), PathName(this), classname \
                );                                                                        \
                SetTypeDef(nullptr);                                                      \
            }                                                                             \
        } else                                                                            \
            SetTypeDef(nullptr);                                                          \
    }
```

---

## 2. Memory Management Macros

### OBJ_MEM_OVERLOAD

**Defined in:** `src/system/utl/MemMgr.h`

**Purpose:** Overloads `new` and `delete` operators for Hmx::Object-derived classes with tracking.

```cpp
#define OBJ_MEM_OVERLOAD(line_num)                                                       \
    static void *operator new(unsigned int s) {                                          \
        return MemAlloc(s, __FILE__, line_num, StaticClassName().Str(), 0);              \
    }                                                                                    \
    static void *operator new(unsigned int s, void *place) { return place; }             \
    static void operator delete(void *v) {                                               \
        MemFree(v, __FILE__, line_num, StaticClassName().Str());                         \
    }
```

**Parameters:**
| Name | Description |
|------|-------------|
| `line_num` | The line number in the original source (must match binary) |

**Usage:**
```cpp
class MyObject : public Hmx::Object {
public:
    OBJ_MEM_OVERLOAD(42);  // Line number must match original
};
```

**Pitfall:** The line number is embedded in the binary. Changing it will cause a mismatch.

---

### MEM_OVERLOAD

**Defined in:** `src/system/utl/MemMgr.h`

**Purpose:** Memory operator overload for non-Object classes.

```cpp
#define MEM_OVERLOAD(class_name, line_num)                                               \
    static void *operator new(unsigned int s) {                                          \
        return MemAlloc(s, __FILE__, line_num, #class_name, 0);                          \
    }                                                                                    \
    static void *operator new(unsigned int s, void *place) { return place; }             \
    static void operator delete(void *v) { MemFree(v, __FILE__, line_num, #class_name); }
```

**Usage:**
```cpp
class TypeProps : public ObjRefOwner {
public:
    MEM_OVERLOAD(TypeProps, 0x485);
};
```

---

### RELEASE

**Defined in:** `src/macros.h`

**Purpose:** Delete an object and set the pointer to null.

```cpp
#define RELEASE(x) (delete x, x = null)
```

**Usage:**
```cpp
BeatClock::~BeatClock() { RELEASE(mMeasureMap); }
```

---

## 3. Message Handler Macros

Message handlers process script messages sent to objects.

### BEGIN_HANDLERS / END_HANDLERS

**Defined in:** `src/system/obj/Object.h`

**Purpose:** Define the `Handle()` method for processing messages.

```cpp
#define BEGIN_HANDLERS(objType)                                                          \
    DataNode objType::Handle(DataArray *_msg, bool _warn) {                              \
        Symbol sym = _msg->Sym(1);                                                       \
        MessageTimer timer(                                                              \
            (MessageTimer::Active()) ? static_cast<Hmx::Object *>(this) : 0, sym         \
        );

#define END_HANDLERS                                                                     \
    if (_warn)                                                                           \
        MILO_NOTIFY("%s unhandled msg: %s", PathName(this), sym);                        \
    return DATA_UNHANDLED;                                                               \
    }
```

---

### HANDLE

**Defined in:** `src/system/obj/Object.h`

**Purpose:** Handle a message by calling a member function.

```cpp
#define HANDLE(s, func)                                                                  \
    {                                                                                    \
        _NEW_STATIC_SYMBOL(s)                                                            \
        if (sym == _s)                                                                   \
            _HANDLE_CHECKED(func(_msg))                                                  \
    }
```

**Usage:**
```cpp
BEGIN_HANDLERS(BeatClock)
    HANDLE(sync, OnSyncState)  // Calls OnSyncState(_msg)
END_HANDLERS
```

---

### HANDLE_ACTION

**Defined in:** `src/system/obj/Object.h`

**Purpose:** Handle a message with an inline action that returns 0.

```cpp
#define HANDLE_ACTION(s, action)                                                         \
    {                                                                                    \
        _NEW_STATIC_SYMBOL(s)                                                            \
        if (sym == _s) {                                                                 \
            (action);                                                                    \
            return 0;                                                                    \
        }                                                                                \
    }
```

**Usage:**
```cpp
BEGIN_HANDLERS(BeatClock)
    HANDLE_ACTION(start, unk54 = true)
    HANDLE_ACTION(pause, unk54 = false)
    HANDLE_ACTION(reset, Reset())
END_HANDLERS
```

---

### HANDLE_EXPR

**Defined in:** `src/system/obj/Object.h`

**Purpose:** Handle a message by returning an expression value.

```cpp
#define HANDLE_EXPR(s, expr)                                                             \
    {                                                                                    \
        _NEW_STATIC_SYMBOL(s)                                                            \
        if (sym == _s)                                                                   \
            return expr;                                                                 \
    }
```

**Usage:**
```cpp
BEGIN_HANDLERS(UIList)
    HANDLE_EXPR(selected_pos, SelectedPos())
    HANDLE_EXPR(num_display, NumDisplay())
    HANDLE_EXPR(is_scrolling, IsScrolling())
END_HANDLERS
```

---

### HANDLE_MESSAGE

**Defined in:** `src/system/obj/Object.h`

**Purpose:** Handle a typed message class (e.g., ButtonDownMsg).

```cpp
#define HANDLE_MESSAGE(msg)                                                              \
    if (sym == msg::Type())                                                              \
    _HANDLE_CHECKED(OnMsg(msg(_msg)))
```

**Usage:**
```cpp
BEGIN_HANDLERS(UIList)
    HANDLE_MESSAGE(ButtonDownMsg)
    // ...
END_HANDLERS

// Requires corresponding OnMsg method:
DataNode UIList::OnMsg(const ButtonDownMsg &msg) {
    // Handle button down
}
```

---

### HANDLE_SUPERCLASS

**Defined in:** `src/system/obj/Object.h`

**Purpose:** Forward unhandled messages to parent class.

```cpp
#define HANDLE_SUPERCLASS(parent) HANDLE_FORWARD(parent::Handle)
```

**Usage:**
```cpp
BEGIN_HANDLERS(BeatClock)
    // Local handlers first
    HANDLE_ACTION(start, unk54 = true)
    // Then forward to parents
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
```

**Note:** Order matters for codegen. Always place HANDLE_SUPERCLASS after local handlers.

---

### HANDLE_MEMBER / HANDLE_MEMBER_PTR

**Defined in:** `src/system/obj/Object.h`

**Purpose:** Forward messages to a member object.

```cpp
#define HANDLE_MEMBER(member) HANDLE_FORWARD(member.Handle)
#define HANDLE_MEMBER_PTR(member)                                                        \
    if (member)                                                                          \
    HANDLE_FORWARD(member->Handle)
```

---

### Complete Handler Example

```cpp
BEGIN_HANDLERS(UIList)
    HANDLE_MESSAGE(ButtonDownMsg)
    HANDLE(selected_sym, OnSelectedSym)
    HANDLE_EXPR(selected_pos, SelectedPos())
    HANDLE_EXPR(selected_data, SelectedData())
    HANDLE_ACTION(set_provider, SetProvider(_msg->Obj<UIListProvider>(2)))
    HANDLE(set_data, OnSetData)
    HANDLE_ACTION(refresh, Refresh(true))
    HANDLE_ACTION(store, Store())
    HANDLE_SUPERCLASS(ScrollSelect)
    HANDLE_SUPERCLASS(UIComponent)
END_HANDLERS
```

---

## 4. Property Sync Macros

Property sync macros implement the `SyncProperty()` method for editor/script property access.

### BEGIN_PROPSYNCS / END_PROPSYNCS

**Defined in:** `src/system/obj/Object.h`

```cpp
#define BEGIN_PROPSYNCS(objType)                                                         \
    bool objType::SyncProperty(DataNode &_val, DataArray *_prop, int _i, PropOp _op) {   \
        if (_i == _prop->Size())                                                         \
            return true;                                                                 \
        else {                                                                           \
            Symbol sym = _prop->Sym(_i);

#define END_PROPSYNCS                                                                    \
    return false;                                                                        \
    }                                                                                    \
    }
```

---

### SYNC_PROP

**Purpose:** Sync a property to a member variable directly.

```cpp
#define SYNC_PROP(s, member)                                                             \
    {                                                                                    \
        _NEW_STATIC_SYMBOL(s)                                                            \
        if (sym == _s)                                                                   \
            return PropSync(member, _val, _prop, _i + 1, _op);                           \
    }
```

**Usage:**
```cpp
BEGIN_PROPSYNCS(BeatClock)
    SYNC_PROP(bpm, mBeatsPerMinute)
    SYNC_PROP(measures_per_phrase, mMeasuresPerPhrase)
END_PROPSYNCS
```

---

### SYNC_PROP_SET

**Purpose:** Sync with a custom setter action when kPropSet is used.

```cpp
#define SYNC_PROP_SET(s, member, func)                                                   \
    {                                                                                    \
        _NEW_STATIC_SYMBOL(s)                                                            \
        if (sym == _s) {                                                                 \
            if (_op == kPropSet) {                                                       \
                func;                                                                    \
            } else {                                                                     \
                if (_op == (PropOp)0x40)                                                 \
                    return false;                                                        \
                _val = member;                                                           \
            }                                                                            \
            return true;                                                                 \
        }                                                                                \
    }
```

**Usage:**
```cpp
BEGIN_PROPSYNCS(BeatClock)
    SYNC_PROP_SET(beats_per_measure, mBeatsPerMeasure, SetBeatsPerMeasure(_val.Int()))
END_PROPSYNCS
```

---

### SYNC_PROP_MODIFY

**Purpose:** Sync with a callback when the property is modified (set/insert/remove).

```cpp
#define SYNC_PROP_MODIFY(s, member, func)                                                \
    {                                                                                    \
        _NEW_STATIC_SYMBOL(s)                                                            \
        if (sym == _s) {                                                                 \
            if (PropSync(member, _val, _prop, _i + 1, _op)) {                            \
                if (!(_op & (kPropSize | kPropGet))) {                                   \
                    func;                                                                \
                }                                                                        \
                return true;                                                             \
            } else {                                                                     \
                return false;                                                            \
            }                                                                            \
        }                                                                                \
    }
```

**Usage:**
```cpp
BEGIN_PROPSYNCS(BeatClock)
    SYNC_PROP_MODIFY(use_global, mUseGlobal, mSound = nullptr)
    SYNC_PROP_MODIFY(sound, mSound, mUseGlobal = false)
END_PROPSYNCS
```

---

### SYNC_PROP_BITFIELD

**Purpose:** Sync individual bits within a bitfield member.

```cpp
#define SYNC_PROP_BITFIELD(symbol, mask_member, line_num)                                \
    { _NEW_STATIC_SYMBOL(symbol) _SYNC_PROP_BITFIELD(_s, mask_member, line_num) }
```

**Parameters:**
| Name | Description |
|------|-------------|
| `symbol` | Property name |
| `mask_member` | The integer member holding the bitfield |
| `line_num` | Line number for assertions (must match original) |

---

### SYNC_SUPERCLASS

**Purpose:** Forward property sync to parent class.

```cpp
#define SYNC_SUPERCLASS(parent)                                                          \
    if (parent::SyncProperty(_val, _prop, _i, _op))                                      \
        return true;
```

**Usage:**
```cpp
BEGIN_PROPSYNCS(BeatClock)
    SYNC_PROP(bpm, mBeatsPerMinute)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS
```

---

## 5. Serialization Macros (Save/Load)

### INIT_REVS

**Defined in:** `src/system/obj/Object.h`

**Purpose:** Define revision constants for version checking.

```cpp
#define INIT_REVS(rev, alt)                                                              \
    static const __declspec(align(4)) unsigned short gRev = rev;                         \
    static const __declspec(align(4)) unsigned short gAltRev = alt;
```

**Usage:**
```cpp
INIT_REVS(3, 0)  // Main revision 3, alt revision 0

BEGIN_LOADS(BeatClock)
    LOAD_REVS(bs)
    ASSERT_REVS(3, 0)
    // ...
END_LOADS
```

**Important:** Place `INIT_REVS` after the `Save()` and `Load()` implementations, not before.

---

### BEGIN_SAVES / END_SAVES

**Purpose:** Define the `Save()` method.

```cpp
#define BEGIN_SAVES(objType) void objType::Save(BinStream &bs) {
#define END_SAVES }
```

---

### SAVE_REVS

**Purpose:** Write revision numbers to the stream.

```cpp
#define SAVE_REVS(rev, alt) bs << packRevs(alt, rev);
```

---

### SAVE_SUPERCLASS

**Purpose:** Call parent class Save method.

```cpp
#define SAVE_SUPERCLASS(parent) parent::Save(bs);
```

---

### Save Example

```cpp
BEGIN_SAVES(BeatClock)
    SAVE_REVS(3, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mBeatsPerMinute;
    bs << mBeatsPerMeasure;
    bs << mUseGlobal;
    bs << mMeasuresPerPhrase;
    bs << mSound;
    bs << mTimeline;
END_SAVES
```

---

### BEGIN_LOADS / END_LOADS

**Purpose:** Define the `Load()` method.

```cpp
#define BEGIN_LOADS(objType) void objType::Load(BinStream &bs) {
#define END_LOADS }
```

---

### LOAD_REVS

**Purpose:** Read and unpack revision numbers.

```cpp
#define LOAD_REVS(bs)                                                                    \
    int revs;                                                                            \
    bs >> revs;                                                                          \
    BinStreamRev d(bs, revs);
```

After this macro, use `d.rev` for main revision and `d.altRev` for alt revision.

---

### ASSERT_REVS

**Purpose:** Validate that loaded revisions don't exceed expected values.

```cpp
#define ASSERT_REVS(rev1, rev2)                                                          \
    if (d.rev > rev1) {                                                                  \
        MILO_FAIL(                                                                       \
            "%s can't load new %s version %d > %d",                                      \
            PathName(this),                                                              \
            ClassName(),                                                                 \
            d.rev,                                                                       \
            gRev                                                                         \
        );                                                                               \
    }                                                                                    \
    if (d.altRev > rev2) {                                                               \
        MILO_FAIL(                                                                       \
            "%s can't load new %s alt version %d > %d",                                  \
            PathName(this),                                                              \
            ClassName(),                                                                 \
            d.altRev,                                                                    \
            gAltRev                                                                      \
        );                                                                               \
    }
```

---

### LOAD_SUPERCLASS

**Purpose:** Call parent class Load method.

```cpp
#define LOAD_SUPERCLASS(parent) parent::Load(d.stream);
```

**Note:** Uses `d.stream` (the wrapped BinStream), not `bs` directly.

---

### LOAD_BITFIELD / LOAD_BITFIELD_ENUM

**Purpose:** Load bitfield members that can't be read directly.

```cpp
#define LOAD_BITFIELD(type, name)                                                        \
    {                                                                                    \
        type bs_name;                                                                    \
        d >> bs_name;                                                                    \
        name = bs_name;                                                                  \
    }

#define LOAD_BITFIELD_ENUM(type, name, enum_name)                                        \
    {                                                                                    \
        type bs_name;                                                                    \
        d >> bs_name;                                                                    \
        name = (enum_name)bs_name;                                                       \
    }
```

---

### Load Example

```cpp
INIT_REVS(3, 0)

BEGIN_LOADS(BeatClock)
    LOAD_REVS(bs)
    ASSERT_REVS(3, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    d >> mBeatsPerMinute;
    d >> mBeatsPerMeasure;
    d >> mUseGlobal;
    if (d.rev >= 1) {
        d >> mMeasuresPerPhrase;
    }
    if (d.rev >= 2) {
        mSound.Load(d.stream, true, nullptr);
    }
    if (d.rev >= 3) {
        d >> (int &)mTimeline;
    }
    SetBeatsPerMeasure(mBeatsPerMeasure);
END_LOADS
```

---

## 6. Object Copying Macros

### BEGIN_COPYS / END_COPYS

**Purpose:** Define the `Copy()` method.

```cpp
#define BEGIN_COPYS(objType)                                                             \
    void objType::Copy(const Hmx::Object *o, Hmx::Object::CopyType ty) {
#define END_COPYS }
```

---

### COPY_SUPERCLASS

**Purpose:** Call parent class Copy method.

```cpp
#define COPY_SUPERCLASS(parent) parent::Copy(o, ty);
```

---

### CREATE_COPY

**Purpose:** Cast the source object to the correct type.

```cpp
#define CREATE_COPY(objType) const objType *c = dynamic_cast<const objType *>(o);
```

---

### BEGIN_COPYING_MEMBERS / END_COPYING_MEMBERS

**Purpose:** Guard member copying with a null check.

```cpp
#define BEGIN_COPYING_MEMBERS if (c) {
#define END_COPYING_MEMBERS }
```

---

### COPY_MEMBER

**Purpose:** Copy a single member from source.

```cpp
#define COPY_MEMBER(mem) mem = c->mem;
```

---

### Copy Example

```cpp
BEGIN_COPYS(BeatClock)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(BeatClock)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mBeatsPerMinute)
        COPY_MEMBER(mBeatsPerMeasure)
        COPY_MEMBER(mMeasuresPerPhrase)
        COPY_MEMBER(mUseGlobal)
        COPY_MEMBER(mTotalSeconds)
        COPY_MEMBER(mSound)
        COPY_MEMBER(mTimeline)
    END_COPYING_MEMBERS
END_COPYS
```

---

## 7. Debug/Assertion Macros

### MILO_ASSERT

**Defined in:** `src/system/os/Debug.h`

**Purpose:** Assert a condition with file and line info.

```cpp
#define MILO_ASSERT(cond, line)                                                          \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            TheDebugFailer << MakeString(kAssertStr, __FILE__, line, #cond);             \
        }                                                                                \
    } while (0)
```

**Parameters:**
| Name | Description |
|------|-------------|
| `cond` | The condition to check |
| `line` | Line number (must match original binary) |

**Usage:**
```cpp
MILO_ASSERT(mListDir, 0x238);
MILO_ASSERT(pMainList, 0x3FD);
```

**Critical:** The condition string `#cond` is baked into the binary. Do not modify the condition expression without verification.

---

### MILO_ASSERT_FMT

**Purpose:** Assert with a custom format message.

```cpp
#define MILO_ASSERT_FMT(cond, ...)                                                       \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            TheDebugFailer << MakeString(__VA_ARGS__);                                   \
        }                                                                                \
    } while (0)
```

**Usage:**
```cpp
MILO_ASSERT_FMT(
    strneq("BIT_", bitsym.Str(), 4),
    "%s does not begin with BIT_",
    bitsym.Str()
);
```

---

### MILO_ASSERT_RANGE / MILO_ASSERT_RANGE_EQ

**Purpose:** Assert value is within a range.

```cpp
// (min) <= (value) && (value) < (max)
#define MILO_ASSERT_RANGE(value, min, max, line)                                         \
    MILO_ASSERT((min) <= (value) && (value) < (max), line)

// (min) <= (value) && (value) <= (max)
#define MILO_ASSERT_RANGE_EQ(value, min, max, line)                                      \
    MILO_ASSERT((min) <= (value) && (value) <= (max), line)
```

---

### MILO_FAIL

**Purpose:** Trigger a fatal error.

```cpp
#define MILO_FAIL(...) TheDebugFailer << MakeString(__VA_ARGS__)
```

**Usage:**
```cpp
MILO_FAIL("DataSymbol() not implemented in UIList provider");
MILO_FAIL("Couldn't instantiate class %s", T::StaticClassName());
```

---

### MILO_WARN

**Purpose:** Print a warning message.

```cpp
#define MILO_WARN(...) TheDebugWarner << MakeString(__VA_ARGS__)
```

---

### MILO_NOTIFY

**Purpose:** Print a notification message.

```cpp
#define MILO_NOTIFY(...) TheDebugNotifier << MakeString(__VA_ARGS__)
```

**Usage:**
```cpp
MILO_NOTIFY("Couldn't find %s in UIList provider", sym);
```

---

### MILO_TRY / MILO_CATCH

**Purpose:** Exception handling with debug integration.

```cpp
#define MILO_TRY                                                                         \
    try {                                                                                \
        TheDebug.SetTry(true);                                                           \
        do

#define MILO_CATCH(name)                                                                 \
    while (false)                                                                        \
        ;                                                                                \
    TheDebug.SetTry(false);                                                              \
    }                                                                                    \
    catch (const char *name)
```

**Usage:**
```cpp
MILO_TRY {
    // Code that may throw
} MILO_CATCH(errMsg) {
    MILO_NOTIFY("An unexpected thing happened: %s", errMsg);
}
```

---

## 8. Iteration Macros

### FOREACH

**Defined in:** `src/system/utl/Std.h`

**Purpose:** Forward iteration over a container.

```cpp
#define FOREACH(it, container) FOREACH_(it, container, ++it)
```

**Usage:**
```cpp
FOREACH (it, mRunningNodes) {
    if ((*it)->ClassName() != FlowLabel::StaticClassName()) {
        (*it)->RequestStopCancel();
    }
}
```

---

### FOREACH_CONST

**Purpose:** Forward const iteration (creates const reference to container).

```cpp
#define FOREACH_CONST(it, container) FOREACH_CONST_(it, container, ++it)
```

---

### FOREACH_REVERSE

**Purpose:** Reverse iteration.

```cpp
#define FOREACH_REVERSE(it, container) FOREACH_REVERSE_(it, container, ++it)
```

---

### FOREACH_PTR

**Purpose:** Iteration over pointer-to-container.

```cpp
#define FOREACH_PTR(it, container) FOREACH_PTR_(it, container, ++it)
```

---

### POST Variants

The `_POST` variants use post-increment (`it++`) instead of pre-increment:

- `FOREACH_POST`
- `FOREACH_CONST_POST`
- `FOREACH_REVERSE_POST`
- `FOREACH_PTR_POST`

---

## Complete Class Example

Here's a complete example showing all major macro categories together:

```cpp
// MyClass.h
class MyClass : public Hmx::Object {
public:
    OBJ_CLASSNAME(MyClass);
    OBJ_SET_TYPE(MyClass);
    OBJ_MEM_OVERLOAD(42);  // Must match original line number
    NEW_OBJ(MyClass);

    MyClass();
    virtual ~MyClass();

    virtual DataNode Handle(DataArray *_msg, bool _warn);
    virtual bool SyncProperty(DataNode &_val, DataArray *_prop, int _i, PropOp _op);
    virtual void Save(BinStream &bs);
    virtual void Load(BinStream &bs);
    virtual void Copy(const Hmx::Object *o, Hmx::Object::CopyType ty);

private:
    int mValue;
    float mSpeed;
    ObjPtr<Hmx::Object> mTarget;
};

// MyClass.cpp
#include "MyClass.h"

MyClass::MyClass() : mValue(0), mSpeed(1.0f), mTarget(this) {}
MyClass::~MyClass() {}

BEGIN_HANDLERS(MyClass)
    HANDLE_ACTION(reset, mValue = 0)
    HANDLE_EXPR(get_value, mValue)
    HANDLE_ACTION(set_value, mValue = _msg->Int(2))
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(MyClass)
    SYNC_PROP(value, mValue)
    SYNC_PROP_SET(speed, mSpeed, SetSpeed(_val.Float()))
    SYNC_PROP(target, mTarget)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(MyClass)
    SAVE_REVS(2, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mValue;
    bs << mSpeed;
    bs << mTarget;
END_SAVES

BEGIN_COPYS(MyClass)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(MyClass)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mValue)
        COPY_MEMBER(mSpeed)
        COPY_MEMBER(mTarget)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(2, 0)

BEGIN_LOADS(MyClass)
    LOAD_REVS(bs)
    ASSERT_REVS(2, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    d >> mValue;
    if (d.rev >= 2) {
        d >> mSpeed;
    }
    mTarget.Load(d.stream, true, nullptr);
END_LOADS
```

---

## Source File Index

| Macro Category | Primary Header |
|----------------|----------------|
| Object Lifecycle | `src/system/obj/Object.h` |
| Memory Management | `src/system/utl/MemMgr.h` |
| Message Handlers | `src/system/obj/Object.h` |
| Property Sync | `src/system/obj/Object.h` |
| Serialization | `src/system/obj/Object.h` |
| Object Copying | `src/system/obj/Object.h` |
| Debug/Assertions | `src/system/os/Debug.h` |
| Iteration | `src/system/utl/Std.h` |
| Utility | `src/macros.h` |
