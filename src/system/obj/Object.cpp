#include "obj/Object.h"
#include "Dir.h"
#include "Msg.h"
#include "obj/Object.h"
#include "obj\Data.h"
#include "obj\DataFunc.h"
#include "obj\Utl.h"
#include "os\Debug.h"
#include "os\File.h"
#include "os\OSFuncs.h"
#include "os\Platform.h"
#include "os\System.h"
#include "utl/BinStream.h"
#include "utl\Symbol.h"

#ifdef HX_NATIVE
#include <vector>
// Declared at global scope: a block-scope extern inside a Hmx::Object member
// would bind to Hmx::SoundAudioTraceOn (unresolved) instead of the global
// definition in Sound.cpp.
extern bool SoundAudioTraceOn();
Hmx::Object *Hmx::Object::sDeleting;
bool Hmx::Object::sRingsDirty = false;
bool gInReplaceList = false;

// Check if an ObjRef's alive sentinel is still set. Reads potentially freed
// memory during cascading destruction — suppress ASAN for this specific check.
// Under glibc, freed memory is typically zeroed → sentinel reads as 0 → dead.
// Snapshot ring entries into a vector, skipping freed nodes.
// During cascading ObjectDir destruction, some ObjRefs may be freed but still
// linked in the ring (~ObjRefConcrete skipped Release). Their mAliveSentinel
// was cleared by ~ObjRef(). We read the sentinel and next pointer from these
// freed nodes — suppress ASAN since the memory is quarantined but readable.
#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
__attribute__((noinline, no_sanitize("address")))
#endif
static void SnapshotRing(ObjRef *sentinel, std::vector<ObjRef *> &out) {
    constexpr size_t kSentinelOffset = 3 * sizeof(void *);
    constexpr size_t kNextOffset = sizeof(void *);
    constexpr size_t kMaxRingSize = 100000; // safety limit
    ObjRef *first = *(ObjRef **)((const char *)sentinel + kNextOffset);
    size_t count = 0;
    for (ObjRef *it = first; it != sentinel; ) {
        if ((uintptr_t)it < 0x10000)
            break;
        if (++count > kMaxRingSize) {
            MILO_LOG("SnapshotRing: RING CORRUPTION — walked %zu nodes without returning to sentinel %p (first=%p, cur=%p)\n",
                count, (void*)sentinel, (void*)first, (void*)it);
            break;
        }
        // Read mAliveSentinel to check if node was freed
        uint32_t alive = *(const uint32_t *)((const char *)it + kSentinelOffset);
        ObjRef *nextNode = *(ObjRef **)((const char *)it + kNextOffset);
        if (alive == 0xCAFEBABE)
            out.push_back(it);
        it = nextNode;
    }
}

// ---------------------------------------------------------------------------
// RefAudit -- opt-in shadow index of "which ObjRef targets which object".
// See the contract in Object.h. DC3_REFRING_AUDIT=1 to enable; the cost when
// disabled is one predictable branch per ref mutation.
// ---------------------------------------------------------------------------
#include <execinfo.h>
#include <unordered_map>
#include <unordered_set>

namespace RefAudit {
    namespace {
        struct NodeInfo {
            Hmx::Object *target;
            int depth;
            void *frames[10];
        };
        std::unordered_map<const ObjRef *, NodeInfo> &Nodes() {
            static std::unordered_map<const ObjRef *, NodeInfo> m;
            return m;
        }
        std::unordered_map<const Hmx::Object *, std::unordered_set<const ObjRef *> > &
        ByTarget() {
            static std::unordered_map<
                const Hmx::Object *, std::unordered_set<const ObjRef *> >
                m;
            return m;
        }
        void PrintFrames(void *const *frames, int depth) {
            if (depth <= 0)
                return;
            char **syms = backtrace_symbols(frames, depth);
            for (int i = 0; i < depth; i++) {
                MILO_LOG("REFAUDIT      #%d %s\n", i, syms ? syms[i] : "?");
            }
            free(syms);
        }
    }

    bool Enabled() {
        static int cached = -1;
        if (cached < 0) {
            const char *v = getenv("DC3_REFRING_AUDIT");
            cached = (v && *v && *v != '0') ? 1 : 0;
            if (cached) {
                MILO_LOG(
                    "REFAUDIT: DC3_REFRING_AUDIT=1 -- ref-ring shadow index ACTIVE. "
                    "This is a diagnostic; it costs a hash op per ref mutation and "
                    "nothing consults it.\n"
                );
            }
        }
        return cached == 1;
    }

    void Retarget(ObjRef *node, Hmx::Object *from, Hmx::Object *to) {
        if (!Enabled())
            return;
        if (from) {
            auto bt = ByTarget().find(from);
            if (bt != ByTarget().end()) {
                bt->second.erase(node);
                if (bt->second.empty())
                    ByTarget().erase(bt);
            }
        }
        if (to) {
            NodeInfo &info = Nodes()[node];
            info.target = to;
            info.depth = backtrace(info.frames, 10);
            ByTarget()[to].insert(node);
        } else {
            Nodes().erase(node);
        }
    }

    void Forget(ObjRef *node) {
        if (!Enabled())
            return;
        auto it = Nodes().find(node);
        if (it == Nodes().end())
            return;
        auto bt = ByTarget().find(it->second.target);
        if (bt != ByTarget().end()) {
            bt->second.erase(node);
            if (bt->second.empty())
                ByTarget().erase(bt);
        }
        Nodes().erase(it);
    }

    void Describe(const ObjRef *node, const char *why) {
        if (!Enabled())
            return;
        auto it = Nodes().find(node);
        MILO_LOG(
            "REFAUDIT   node=%p (%s) target=%p\n",
            (void *)const_cast<ObjRef *>(node),
            why,
            it == Nodes().end() ? nullptr : (void *)it->second.target
        );
        if (it != Nodes().end())
            PrintFrames(it->second.frames, it->second.depth);
    }

    void Backtrace(const char *why) {
        if (!Enabled())
            return;
        void *frames[16];
        int depth = backtrace(frames, 16);
        MILO_LOG("REFAUDIT backtrace (%s):\n", why);
        PrintFrames(frames, depth);
    }

    void PreWalk(Hmx::Object *obj, const ObjRef *ring) {
        if (!Enabled())
            return;
        auto bt = ByTarget().find(obj);
        if (bt == ByTarget().end())
            return;
        // Collect what the ring can actually reach, using the same
        // read-only, dead-node-skipping traversal the walkers use.
        std::unordered_set<const ObjRef *> reachable;
        std::vector<ObjRef *> snapshot;
        SnapshotRing(const_cast<ObjRef *>(ring), snapshot);
        for (ObjRef *r : snapshot)
            reachable.insert(r);
        for (const ObjRef *node : bt->second) {
            if (reachable.count(node) == 0) {
                MILO_LOG(
                    "REFAUDIT LOST-FROM-RING: obj=%p still targeted by a node the "
                    "ring cannot reach. The ring walk will not nullify it.\n",
                    (void *)obj
                );
                Describe(node, "registered at");
                Backtrace("~Object");
            }
        }
    }

    void PostWalk(Hmx::Object *obj) {
        if (!Enabled())
            return;
        auto bt = ByTarget().find(obj);
        if (bt == ByTarget().end())
            return;
        std::vector<const ObjRef *> leftovers(bt->second.begin(), bt->second.end());
        for (const ObjRef *node : leftovers) {
            MILO_LOG(
                "REFAUDIT WALK-DECLINED: obj=%p still targeted after the ring walk "
                "-- a Replace/Nullify path left the ref pointing at freed memory.\n",
                (void *)obj
            );
            Describe(node, "registered at");
        }
    }
}

void ObjRef::ReplaceList(Hmx::Object *obj) {
    // Suppress ObjPtrVec::erase and Transitions::RemoveNodes during ring walk.
    bool wasInReplace = gInReplaceList;
    gInReplaceList = true;

    while (next != this) {
        ObjRef *cur = next;
        cur->Replace(obj);
        if (cur == next) {
            // Replace didn't advance — force-unlink to prevent infinite loop.
            cur->prev->next = cur->next;
            cur->next->prev = cur->prev;
            cur->prev = cur;
            cur->next = cur;
        }
    }

    gInReplaceList = wasInReplace;
}
#endif

bool gLoadingProxyFromDisk = false;
bool gMiloTool = false;
std::map<Symbol, ObjectFunc *> Hmx::Object::sFactories;
DataArrayPtr gPropPaths[8] = {
    DataArrayPtr(new DataArray(1)), DataArrayPtr(new DataArray(1)),
    DataArrayPtr(new DataArray(1)), DataArrayPtr(new DataArray(1)),
    DataArrayPtr(new DataArray(1)), DataArrayPtr(new DataArray(1)),
    DataArrayPtr(new DataArray(1)), DataArrayPtr(new DataArray(1))
};
MsgSinks gSinks(nullptr);

#pragma region Virtual Methods

Hmx::Object::Object()
    : mTypeProps(nullptr), mTypeDef(nullptr), mName(gNullStr), mDir(nullptr),
      mSinks(nullptr) {
    mRefs.DetachSelf();
#ifdef HX_NATIVE
    mDeathWatch = nullptr;
#endif
}

Hmx::Object::~Object() {
    MILO_ASSERT_FMT(MainThread(), "Can't delete objects outside of the main thread");
#ifdef HX_NATIVE
    // Trip every DeathWatch before anything else in the teardown: a watcher is
    // a frame of OUR OWN that is still on the stack, and it must learn we are
    // gone even if something below re-enters. Unlink as we go so ~DeathWatch
    // never writes back into this block.
    for (Hmx::DeathWatch *w = mDeathWatch; w;) {
        Hmx::DeathWatch *prev = w->mPrev;
        w->mDead = true;
        w = prev;
    }
    mDeathWatch = nullptr;
#endif
    if (mTypeDef) {
        mTypeDef->Release();
        mTypeDef = nullptr;
    }
    ClearAllTypeProps();
    RemoveFromDir();
    RELEASE(mSinks);
    Hmx::Object *old = sDeleting;
    sDeleting = this;
#ifdef HX_NATIVE
    // Nullify, don't skip.
    //
    // ObjectDir::DeleteObjects rests on one invariant: when a cascade destroys
    // an object, every ObjRef pointing at it is nullified before Phase 2 hands
    // the block to free(). Phase 0 (NullifyAllRefs) enforces that only for
    // objects reachable from the dir being deleted.
    //
    // This used to be a bare `if (!ObjectDir::InDeleteObjects())` -- i.e. an
    // object destroyed DURING a cascade but not in the dir's iteration set got
    // NEITHER path, and every holder of a ref to it was left dangling. That is
    // the whole population of cascade collateral: ~AnimTask's blend task,
    // ~Sequence's instruments, ~HamCharacter's mWaypoint, any `delete` reached
    // from a Phase-1 destructor. It is what crashed TaskMgr::Poll, and it is
    // why three separate holders needed site-specific dangling-pointer guards.
    //
    // Calling ReplaceRefs(nullptr) here is what the PPC build does, but it is
    // the wrong tool mid-cascade: it fires Replace callbacks, which re-enter
    // (MessageTask::Replace does `delete this`) while Phase 1 is walking a
    // todo list. NullifyAllRefs is the mechanism Phase 0 already trusts for
    // exactly this situation, and it skips ring nodes whose mAliveSentinel was
    // cleared -- the freed-ObjPtrVec-buffer hazard the old comment cited.
    //
    // NOT callback-free, and do not describe it that way: ObjPtrList::Node and
    // ObjPtrVec::Node override NullifyObj, and in kObjListNoNull mode they
    // unlink from the holder's container (the list variant also `delete this`).
    // What it never does is destroy a referent OBJECT or re-enter ~Object, so
    // it cannot deepen the cascade the way a Replace callback can. The holder's
    // container must still be alive to be mutated -- which holds for the same
    // reason it holds in Phase 0: a node whose owner was already destroyed has
    // a cleared sentinel and is skipped, and Phase 2 defers every free until
    // the outermost ~ObjectDir returns.
    //
    // Idempotent by construction: Phase 0 leaves the sentinel self-looped, so
    // a second call on a dir-resident object walks zero nodes.
    RefAudit::PreWalk(this, &mRefs);
    if (ObjectDir::InDeleteObjects())
        NullifyAllRefs();
    else
#endif
    ReplaceRefs(nullptr);
#ifdef HX_NATIVE
    RefAudit::PostWalk(this);
#endif
    sDeleting = old;
    if (gDataThis == this) {
        gDataThis = nullptr;
    }
}

bool Hmx::Object::Replace(ObjRef *from, Hmx::Object *to) {
    if (mSinks)
        return mSinks->Replace(from, to);
    else
        return false;
}

BEGIN_HANDLERS(Hmx::Object)
    HANDLE(get, OnGet)
    HANDLE_EXPR(get_array, PropertyArray(_msg->Sym(2)))
    HANDLE_EXPR(size, PropertySize(_msg->Array(2)))
    HANDLE(set, OnSet)
    HANDLE_ACTION(insert, InsertProperty(_msg->Array(2), _msg->Evaluate(3)))
    HANDLE_ACTION(remove, RemoveProperty(_msg->Array(2)))
    HANDLE_ACTION(clear, PropertyClear(_msg->Array(2)))
    HANDLE(append, OnPropertyAppend)
    HANDLE_EXPR(has, Property(_msg->Array(2), false) != nullptr)
    HANDLE_EXPR(prop_handle, HandleProperty(_msg->Array(2), _msg, true))
    HANDLE_ACTION(copy, Copy(_msg->Obj<Hmx::Object>(2), (CopyType)_msg->Int(3)))
    HANDLE_EXPR(class_name, ClassName())
    HANDLE_EXPR(name, mName)
    HANDLE_EXPR(note, mNote)
    HANDLE_ACTION(set_note, SetNote(_msg->Str(2)))
    HANDLE(iterate_refs, OnIterateRefs)
    HANDLE_EXPR(dir, mDir)
    HANDLE_ACTION(
        set_name,
        SetName(_msg->Str(2), _msg->Size() > 3 ? _msg->Obj<ObjectDir>(3) : Dir())
    )
    HANDLE_ACTION(set_type, SetType(_msg->Sym(2)))
    HANDLE_EXPR(is_a, IsASubclass(ClassName(), _msg->Sym(2)))
    HANDLE_EXPR(get_type, Type())
    HANDLE_EXPR(get_heap, AllocHeapName())
    HANDLE(get_types_list, OnGetTypeList)
    HANDLE_ARRAY(mTypeDef)
    HANDLE(add_sink, OnAddSink)
    HANDLE(remove_sink, OnRemoveSink)
    Export(_msg, false);
END_HANDLERS

BEGIN_PROPSYNCS(Hmx::Object)
    SYNC_PROP_SET(name, mName, SetName(_val.Str(), mDir))
    SYNC_PROP_SET(type, Type(), SetType(_val.Sym()))
    SYNC_PROP(sinks, mSinks ? *mSinks : gSinks)
END_PROPSYNCS

void Hmx::Object::InitObject() {
    static DataArray *objects = SystemConfig("objects");
    static Symbol init = "init";
    DataArray *def = ObjectDef(gNullStr)->FindArray(init, false);
    if (def) {
        def->ExecuteScript(1, this, nullptr, 1);
    }
}

void Hmx::Object::Save(BinStream &bs) {
    SaveType(bs);
    SaveRest(bs);
}

void Hmx::Object::SaveType(BinStream &bs) {
    bs << 2;
    bs << Type();
}

void Hmx::Object::SaveRest(BinStream &bs) {
    if (!mTypeProps)
        bs << (DataArray *)nullptr;
    else
        mTypeProps->Save(bs);

    if (mNote.empty() || bs.Cached())
        bs << 0;
    else
        bs << mNote;
}

void Hmx::Object::Copy(const Hmx::Object *o, CopyType ty) {
    if (ty != kCopyFromMax) {
        mNote = o->Note();
        if (ClassName() == o->ClassName()) {
            SetTypeDef(o->TypeDef());
            if (o->HasTypeProps() && !mTypeProps) {
                mTypeProps = new TypeProps(this);
            } else if (!o->HasTypeProps()) {
                if (mTypeProps) {
                    RELEASE(mTypeProps);
                }
            }
            if (mTypeProps) {
                *mTypeProps = *o->mTypeProps;
            }
        } else if (o->TypeDef() || TypeDef()) {
            MILO_NOTIFY(
                "Can't copy type \"%s\" or type props of %s to %s, different classes %s and %s",
                o->Type(),
                Name(),
                o->Name(),
                ClassName(),
                o->ClassName()
            );
        }
    }
}

void Hmx::Object::Load(BinStream &bs) {
    LoadType(bs);
    LoadRest(bs);
}

INIT_REVS(2, 0)

void Hmx::Object::LoadType(BinStream &bs) {
    LOAD_REVS(bs)
    ASSERT_REVS(2, 0)
    Symbol s;
    bs >> s;
    SetType(s);
    bs.PushRev(packRevs(d.altRev, d.rev), this);
}

void Hmx::Object::LoadRest(BinStream &bs) {
    BinStreamRev d(bs, bs.PopRev(this));
    if (!mTypeProps) {
        mTypeProps = new TypeProps(this);
    }
    mTypeProps->Load(d);
    if (!mTypeProps->HasProps()) {
        RELEASE(mTypeProps);
    }
    if (d.rev > 0) {
        d >> mNote;
    }
}

void Hmx::Object::Export(DataArray *a, bool b) {
    if (b)
        HandleType(a);
    if (mSinks)
        mSinks->Export(a);
}

void Hmx::Object::SetTypeDef(DataArray *def) {
    if (mTypeDef != def) {
        if (mTypeDef) {
            mTypeDef->Release();
            mTypeDef = nullptr;
        }
        ClearAllTypeProps();
        mTypeDef = def;
        if (mTypeDef) {
            mTypeDef->AddRef();
        }
    }
}

DataArray *Hmx::Object::ObjectDef(Symbol s) {
    if (s == gNullStr) {
        return SystemConfig("objects", ClassName());
    } else {
        return SystemConfig("objects", s);
    }
}

void Hmx::Object::SetName(const char *name, ObjectDir *dir) {
    RemoveFromDir();
    if (!name || *name == '\0') {
        mName = gNullStr;
        mDir = nullptr;
    } else {
        MILO_ASSERT(dir, 0xE7);
        mDir = dir;
        ObjectDir::Entry *entry = dir->FindEntry(name, true);
        if (entry->obj) {
            MILO_FAIL("%s already exists", name);
        }
        entry->obj = this;
        mName = entry->name;
        dir->AddedObject(this);
    }
}

ObjectDir *Hmx::Object::DataDir() {
    return mDir ? mDir : ObjectDir::Main();
}

const char *Hmx::Object::FindPathName() {
    const char *name = (mName && *mName) ? mName : ClassName().Str();

    ObjectDir *dataDir = DataDir();
    if (dataDir) {
        DirLoader *loader = dataDir->Loader();
        if (loader) {
            return MakeString(
                "%s (%s)",
                name,
                FileLocalize(loader->LoaderFile().c_str(), nullptr)
            );
        } else if (!dataDir->ProxyFile().empty()) {
            return MakeString(
                "%s (%s)", name, FileLocalize(dataDir->ProxyFile().c_str(), nullptr)
            );
        } else if (*dataDir->GetPathName() != '\0') {
            return MakeString(
                "%s (%s)", name, FileLocalize(dataDir->GetPathName(), nullptr)
            );
        } else if (dataDir != this && dataDir->Name() && *dataDir->Name()) {
            return MakeString("%s/%s", dataDir->Name(), name);
        } else if (mDir && *mDir->GetPathName()) {
            return MakeString("%s (%s)", name, FileLocalize(mDir->GetPathName(), nullptr));
        }
    }
    return name;
}

#pragma region Ref Methods

void Hmx::Object::ReplaceRefs(Hmx::Object *obj) {
    if (mRefs.begin() != mRefs.end()) {
#ifdef HX_NATIVE
        // Snapshot approach: copy ring entries to a vector, then iterate.
        // Immune to ring modifications during Replace callbacks (owners may
        // delete ObjRefs, modify other ring entries, or trigger cascading
        // destructions). The mAliveSentinel field (set in ObjRef constructor,
        // cleared in ~ObjRef) detects freed entries in the snapshot.
        bool wasInReplace = gInReplaceList;
        gInReplaceList = true;
        std::vector<ObjRef *> snapshot;
        SnapshotRing(&mRefs, snapshot);
        mRefs.Clear();
        for (ObjRef *ref : snapshot) {
            // Self-loop each ref so that Release() inside SetObj() writes
            // to itself (harmless) instead of to ring prev/next neighbors
            // that may reside in already-freed objects. The ring has been
            // cleared above, so maintaining ring structure is unnecessary.
            ref->next = ref;
            ref->prev = ref;
            ref->Replace(obj);
        }
        gInReplaceList = wasInReplace;
#else
        ObjRef other(mRefs);
        other.prev->next = &other;
        other.next->prev = &other;
        mRefs.Clear();
        other.ReplaceList(obj);
#endif
    }
}

#ifdef HX_NATIVE
#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
__attribute__((no_sanitize("address")))
#endif
void Hmx::Object::NullifyAllRefs() {
    ObjRef *sentinel = &mRefs;
    constexpr size_t kNextOffset = sizeof(void *); // ObjRef layout: vtable, next, prev, sentinel
    constexpr size_t kSentinelOffset = 3 * sizeof(void *);
    constexpr size_t kMaxRingSize = 100000;
    ObjRef *cur = sentinel->next;
    size_t count = 0;
    while (cur != sentinel) {
        if ((uintptr_t)cur < 0x10000 || ++count > kMaxRingSize)
            break;
        // Read next pointer before potentially dead memory is reused.
        // Dead ObjRefs (from freed ObjPtrVec buffers during cascade) are
        // still linked in the ring — skip them, but keep walking via their
        // next pointer (same technique as SnapshotRing).
        ObjRef *nxt = *(ObjRef **)((const char *)cur + kNextOffset);
        uint32_t alive = *(const uint32_t *)((const char *)cur + kSentinelOffset);
        if (alive == ObjRef::kAliveSentinel)
            cur->NullifyObj();
        cur = nxt;
    }
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
}
#endif

void Hmx::Object::ReplaceRefsFrom(Hmx::Object *from, Hmx::Object *to) {
    MILO_ASSERT(from, 0xA6);
    ObjRef other;
    other.DetachSelf();
    FOREACH (it, mRefs) {
#ifdef HX_NATIVE
        // Virtual base offsets can make RefOwner() != from even for the same
        // object (Itanium ABI vbase adjustment). Use dynamic_cast<void*> to
        // compare most-derived addresses.
        bool match = (it->RefOwner() == from);
        if (!match && it->RefOwner() && from) {
            match = dynamic_cast<const void *>(it->RefOwner())
                 == dynamic_cast<const void *>(from);
        }
        if (match) {
#else
        if (it->RefOwner() == from) {
#endif
            it->Release(&other);
            other.AddRef(it);
        }
    }
    other.ReplaceList(to);
}

int Hmx::Object::RefCount() const {
    int size = 0;
    FOREACH (it, mRefs) {
        size++;
    }
    return size;
}

#pragma endregion
#pragma region Sink Methods

void Hmx::Object::RemovePropertySink(Hmx::Object *o, DataArray *a) {
    if (mSinks)
        mSinks->RemovePropertySink(o, a);
}

bool Hmx::Object::HasPropertySink(Hmx::Object *o, DataArray *a) {
    if (mSinks)
        return mSinks->HasPropertySink(o, a);
    else
        return false;
}

void Hmx::Object::RemoveSink(Hmx::Object *o, Symbol s) {
    if (mSinks)
        mSinks->RemoveSink(o, s);
}

MsgSinks *Hmx::Object::GetOrAddSinks() {
    if (!mSinks) {
        mSinks = new MsgSinks(this);
    }
    return mSinks;
}

void Hmx::Object::AddSink(Hmx::Object *o, Symbol s1, Symbol s2, SinkMode sm, bool b) {
    GetOrAddSinks()->AddSink(o, s1, s2, sm, b);
}

void Hmx::Object::AddPropertySink(Hmx::Object *o, DataArray *a, Symbol s) {
    GetOrAddSinks()->AddPropertySink(o, a, s);
}

void Hmx::Object::MergeSinks(Hmx::Object *o) {
    // (int) cast produces signed cmpwi; direct pointer produces unsigned cmplwi
#ifdef HX_NATIVE
    if (o && o->mSinks) {
#else
    if (o && (int)o->mSinks) {
#endif
        GetOrAddSinks()->MergeSinks(o);
    }
}

void Hmx::Object::ChainSource(Hmx::Object *source, Hmx::Object *o2) {
    MILO_ASSERT(source, 0x29D);
    if (!o2)
        o2 = this;
    if (mSinks && !mSinks->Sinks().empty()) {
        source->GetOrAddSinks()->AddSink(this, Symbol());
    } else if (o2->mSinks) {
        o2->mSinks->ChainEventSinks(source, this);
    }
}

void Hmx::Object::ExportPropertyChange(DataArray *a, Symbol s) {
#ifdef HX_NATIVE
    // Opt-in DC3_AUDIO_TRACE probe (gate lives in Sound.cpp): does a
    // game_stage change have a registered propsync sink handler, and export?
    {
        if (SoundAudioTraceOn() && a && a->Size() > 0
            && a->Node(0).Type() == kDataSymbol) {
            static Symbol game_stage("game_stage");
            if (a->Sym(0) == game_stage) {
                MILO_LOG("AUDIOTRACE PropChange %s.game_stage handler='%s' sinks=%d\n",
                         PathName(this), s.Str(), mSinks ? 1 : 0);
            }
        }
    }
#endif
    if (!s.Null()) {
        MILO_ASSERT_EXPR(mSinks, 0x17F);
        static Message msg("blah", 0);
        msg.SetType(s);
        msg[0] = a;
        Export(msg, true);
    }
}

void Hmx::Object::BroadcastPropertyChange(DataArray *a) {
    ExportPropertyChange(a, mSinks ? mSinks->GetPropSyncHandler(a) : Symbol());
}

#pragma endregion
#pragma region Property Methods

DataArray *GetNextPropPath() {
    for (int i = 0; i < DIM(gPropPaths); i++) {
        if (gPropPaths[i]->RefCount() == 1) {
            return gPropPaths[i];
        }
    }
    MILO_FAIL("Recursive SetProperty call count greater than %d!", DIM(gPropPaths));
    return nullptr;
}

const DataNode *Hmx::Object::Property(DataArray *prop, bool fail) const {
    static DataNode n(0);
    // if prop was synced, return the prop node n
    if (const_cast<Hmx::Object *>(this)->SyncProperty(n, prop, 0, kPropGet))
        return &n;
    Symbol propKey = prop->Sym(0);
    const DataNode *propValue = nullptr;
    if (mTypeProps) {
        // retrieve property val from typeprops array
        propValue = mTypeProps->KeyValue(propKey, false);
    }
    if (!propValue && mTypeDef) {
        DataArray *found = mTypeDef->FindArray(propKey, fail);
        if (found)
            propValue = &found->Evaluate(1);
    }
    if (propValue) {
        int cnt = prop->Size();
        if (cnt == 1)
            return propValue;
        if (cnt == 2 && propValue->Type() == kDataArray) {
            DataArray *ret = propValue->UncheckedArray();
            return &ret->Node(prop->Int(1));
        }
    }

    if (fail) {
        MILO_FAIL_DTA("%s: property %s not found", PathName(this), PrintPropertyPath(prop));
    }
    return nullptr;
}

const DataNode *Hmx::Object::Property(Symbol prop, bool fail) const {
    static DataArrayPtr d(new DataArray(1));
    d->Node(0) = prop;
    return Property(d, fail);
}

DataNode Hmx::Object::HandleProperty(DataArray *prop, DataArray *a2, bool fail) {
    static DataNode n(a2);
    if (SyncProperty(n, prop, 0, kPropHandle)) {
        return n;
    }
    if (fail) {
        MILO_FAIL_DTA(
            "%s: property %s not found", PathName(this), prop ? prop->Sym(0) : "<none>"
        );
    }
    return 0;
}

DataNode Hmx::Object::PropertyArray(Symbol sym) {
    static DataArrayPtr d(new DataArray(1));
    d->Node(0) = sym;
    int size = PropertySize(d);
    DataArray *newArr = new DataArray(size);
    static DataArrayPtr path(new DataArray(2));
    path->Node(0) = sym;
    for (int i = 0; i < size; i++) {
        path->Node(1) = i;
        newArr->Node(i) = *Property(path, true);
    }
    DataNode ret = newArr;
    newArr->Release();
    return ret;
}

int Hmx::Object::PropertySize(DataArray *prop) {
    static DataNode n;
    if (SyncProperty(n, prop, 0, kPropSize)) {
        return n.Int();
    }
    MILO_ASSERT(prop->Size() == 1, 0x208);
    Symbol name = prop->Sym(0);
    const DataNode *a = nullptr;
    if (mTypeProps) {
        a = mTypeProps->KeyValue(name, false);
    }
    if (!a) {
        if (mTypeDef) {
            a = &mTypeDef->FindArray(name)->Evaluate(1);
        } else {
            MILO_FAIL_DTA("%s: property %s not found", PathName(this), name);
#ifdef HX_NATIVE
            return 0; // MILO_FAIL_DTA warns on native, so we must bail before null deref
#endif
        }
    }
    MILO_ASSERT(a->Type() == kDataArray, 0x21B);
    return a->UncheckedArray()->Size();
}

void Hmx::Object::RemoveProperty(DataArray *prop) {
    static DataNode n;
    if (!SyncProperty(n, prop, 0, kPropRemove)) {
        MILO_ASSERT(prop->Size() == 2, 0x235);
        if (mTypeProps) {
            mTypeProps->RemoveArrayValue(prop->Sym(0), prop->Int(1));
        }
    }
}

void Hmx::Object::BroadcastPropertyChange(Symbol s) {
    static DataArray *a = new DataArray(1);
    a->Node(0) = s;
    BroadcastPropertyChange(a);
}

void Hmx::Object::PropertyClear(DataArray *propArr) {
    int size = PropertySize(propArr);
    DataArray *cloned = propArr->Clone(true, false, 1);
    while (size-- != 0) {
        cloned->Node(cloned->Size() - 1) = size;
        RemoveProperty(cloned);
    }
    cloned->Release();
}

void Hmx::Object::SetProperty(DataArray *prop, const DataNode &val) {
    const DataNode *prop_n = nullptr;
    DataNode n;
    Symbol handler;
    if (mSinks) {
        handler = mSinks->GetPropSyncHandler(prop);
        if (!handler.Null()) {
            prop_n = Property(prop, false);
            if (prop_n) {
                n = *prop_n;
            }
        }
    }
    if (!SyncProperty((DataNode &)val, prop, 0, kPropSet)) {
        Symbol key = prop->Sym(0);
        if (!mTypeProps) {
            mTypeProps = new TypeProps(this);
        }
        if (prop->Size() == 1) {
            mTypeProps->SetKeyValue(key, val, true);
        } else {
            MILO_ASSERT(prop->Size() == 2, 0x1C4);
            mTypeProps->SetArrayValue(key, prop->Int(1), val);
        }
        if (prop_n && val.Equal(n, nullptr, false)) {
            handler = Symbol();
        }
    } else {
        if (prop_n) {
            const DataNode *synced = Property(prop, true);
            if (synced->Equal(n, nullptr, false)) {
                handler = Symbol();
            }
        }
    }
    ExportPropertyChange(prop, handler);
}

void Hmx::Object::SetProperty(Symbol prop, const DataNode &val) {
    DataArray *path = GetNextPropPath();
    path->AddRef();
    path->Node(0) = prop;
    SetProperty(path, val);
    path->Release();
}

void Hmx::Object::InsertProperty(DataArray *prop, const DataNode &val) {
    if (!SyncProperty((DataNode &)val, prop, 0, kPropInsert)) {
        MILO_ASSERT(prop->Size() == 2, 0x240);
        if (!mTypeProps) {
            mTypeProps = new TypeProps(this);
        }
        mTypeProps->InsertArrayValue(prop->Sym(0), prop->Int(1), val);
    }
}

#pragma endregion
#pragma region Factory Methods

Hmx::Object *Hmx::Object::NewObject(Symbol name) {
    std::map<Symbol, ObjectFunc *>::iterator it = sFactories.find(name);
    MILO_ASSERT_FMT(it != sFactories.end(), "Unknown class %s", name);
#ifdef HX_NATIVE
    if (it == sFactories.end()) {
        return nullptr;
    }
#endif
    return (it->second)();
}

bool Hmx::Object::RegisteredFactory(Symbol name) {
    return sFactories.find(name) != sFactories.end();
}

void Hmx::Object::RegisterFactory(Symbol name, ObjectFunc *func) {
    sFactories[name] = func;
}

#pragma endregion
#pragma region Misc Methods

void Hmx::Object::SetNote(const char *note) { mNote = note; }

void Hmx::Object::RemoveFromDir() {
    if (mDir && mDir != sDeleting) {
        mDir->RemovingObject(this);
        ObjectDir::Entry *entry = mDir->FindEntry(mName, false);
        if (!entry || entry->obj != this) {
            MILO_FAIL("No entry for %s in %s", PathName(this), PathName(mDir));
#ifdef HX_NATIVE
            // MILO_FAIL stops the title on the 360; on native Debug::Fail
            // prints and returns, so `entry->obj = nullptr` below runs anyway.
            // Both disjuncts of the test above are then unsafe:
            //
            //   entry == nullptr           -> a store through page 0. The 360
            //     maps guest page 0 readable/writable/zeroed and absorbs it;
            //     Linux never maps page 0, so it SIGSEGVs. Same memory-map
            //     difference as the MoveDir::PostUpdateFilters class.
            //
            //   entry->obj != this         -> memory-safe but WORSE: it clears
            //     a hash entry that belongs to a DIFFERENT, live object, so
            //     that object silently vanishes from its dir while still
            //     holding mDir/mName. This is reachable today because the
            //     "%s already exists" MILO_FAIL in SetName() is non-fatal too:
            //     the second object of a duplicate-name pair overwrites
            //     entry->obj, and destroying the FIRST one then unregisters
            //     the second.
            //
            // Neither is what the console does, where the title has already
            // stopped. Leave the dir untouched.
            return;
#endif
        }

        entry->obj = nullptr;
    }
}

bool Hmx::Object::HasTypeProps() const { return mTypeProps && mTypeProps->HasProps(); }

void Hmx::Object::ClearAllTypeProps() { RELEASE(mTypeProps); }

DataNode Hmx::Object::HandleType(DataArray *msg) {
    Symbol t = msg->Sym(1);
    DataArray *handler = nullptr;
    if (mTypeDef) {
        handler = mTypeDef->FindArray(t, false);
    }
    if (handler) {
        MessageTimer timer(this, t);
        return handler->ExecuteScript(1, this, (const DataArray *)msg, 2);
    }
    return DATA_UNHANDLED;
}

#pragma endregion
#pragma region Handlers

DataNode Hmx::Object::OnIterateRefs(const DataArray *da) {
    DataNode *var = da->Var(2);
    DataNode node(*var);
    ObjRef *end = &mRefs;
    for (ObjRef *it = mRefs.next; it != end;) {
        ObjRef *next_it = it->next;
        *var = it->RefOwner();
        for (int i = 3; i < da->Size(); i++) {
            da->Command(i)->Execute();
        }
        it = next_it;
    }
    *var = node;
    return 0;
}

DataNode Hmx::Object::OnGetTypeList(const DataArray *a) {
    DataArray *def = ObjectDef(gNullStr);
    DataArrayPtr ptr;
    static Symbol allow_null_type = "allow_null_type";
    bool b6 = true;
    DataArray *nullArr = def->FindArray(allow_null_type, false);
    if (nullArr) {
        b6 = nullArr->ExecuteScript(1, this, nullptr, 1).Int();
    }
    if (b6) {
        ptr->Insert(ptr->Size(), Symbol());
    }
    DataArray *typesArr = def->FindArray("types", false);
    if (typesArr) {
        for (int i = 1; i < typesArr->Size(); i++) {
            DataArray *curArr = typesArr->Array(i);
            DataArray *helpArr = curArr->FindArray("help", false);
            if (helpArr) {
                DataArray *newArr = new DataArray(2);
                newArr->Node(0) = curArr->Sym(0);
                newArr->Node(1) = helpArr;
                ptr->Insert(ptr->Size(), newArr);
                newArr->Release();
            } else {
                ptr->Insert(ptr->Size(), curArr->Sym(0));
            }
        }
    }
    return ptr;
}

DataNode Hmx::Object::OnAddSink(DataArray *a) {
    if (a->Size() >= 4) {
        SinkMode mode = (a->Size() > 4) ? (SinkMode)a->Int(4) : kHandle;
        bool chain = (a->Size() > 5) ? a->Int(5) : true;
        DataArray *arr3 = a->Array(3);
        Hmx::Object *obj = a->GetObj(2);
        if (obj) {
            if (arr3->Size() == 0) {
                GetOrAddSinks()->AddSink(obj, Symbol(), Symbol(), mode, chain);
            } else {
                for (int i = 0; i < arr3->Size(); i++) {
                    DataNode eval = arr3->Evaluate(i);
                    Symbol s7;
                    Symbol s6;
                    if (eval.Type() == kDataArray) {
                        s6 = eval.LiteralArray()->LiteralSym(1);
                        s7 = eval.LiteralArray()->LiteralSym(0);
                    } else {
                        s6 = Symbol();
                        s7 = eval.LiteralSym();
                    }
                    AddSink(obj, s7, s6, mode, chain);
                }
            }
        }
    } else {
        Symbol s1, s2;
        Hmx::Object *obj = a->GetObj(2);
        AddSink(obj, s1, s2, kHandle, true);
    }
    return 0;
}

DataNode Hmx::Object::OnRemoveSink(DataArray *a) {
    if (a->Size() > 3) {
        Hmx::Object *obj = a->GetObj(2);
        Symbol s;
        for (int i = 3; i < a->Size(); i++) {
            s = a->Sym(i);
            if (mSinks)
                mSinks->RemoveSink(obj, s);
        }
    } else {
        Symbol s = Symbol();
        Hmx::Object *obj = a->GetObj(2);
        if (mSinks)
            mSinks->RemoveSink(obj, s);
    }
    return 0;
}

DataNode Hmx::Object::OnGet(const DataArray *a) {
    const DataNode &node = a->Evaluate(2);
    if (node.Type() == kDataSymbol) {
        const char *sym = node.UncheckedStr();
        const DataNode *prop = Property(STR_TO_SYM(sym), a->Size() < 4);
        if (prop)
            return *prop;
    } else {
        if (node.Type() != kDataArray) {
            String str;
            node.Print(str, true, 0);
            MILO_FAIL(
                "Data %s is not array or symbol (file %s, line %d)",
                str.c_str(),
                a->File(),
                a->Line()
            );
        }
        const DataNode *prop = Property(node.UncheckedArray(), a->Size() < 4);
        if (prop)
            return *prop;
    }
#ifdef HX_NATIVE
    if (a->Size() > 3)
        return a->Node(3);
    return DataNode(0);
#else
    return a->Node(3);
#endif
}

DataNode Hmx::Object::OnSet(const DataArray *a) {
    MILO_ASSERT_FMT(
        a->Size() % 2 == 0,
        "Uneven number of properties (file %s, line %d)",
        a->File(),
        a->Line()
    );
    for (int i = 2; i < a->Size(); i += 2) {
        const DataNode &n = a->Evaluate(i);
        if (n.Type() == kDataSymbol) {
            const DataNode &eval = a->Evaluate(i + 1);
            const char *str = n.UncheckedStr();
            SetProperty(STR_TO_SYM(str), eval);
        } else {
            if (n.Type() != kDataArray) {
                String str;
                n.Print(str, true, 0);
                MILO_FAIL(
                    "Data %s is not array or symbol (file %s, line %d)",
                    str.c_str(),
                    a->File(),
                    a->Line()
                );
            }
            SetProperty(n.UncheckedArray(), a->Evaluate(i + 1));
        }
    }
    return 0;
}

DataNode Hmx::Object::OnPropertyAppend(const DataArray *da) {
    DataArray *arr = da->Array(2);
    int size = PropertySize(arr);
    DataArray *cloned = arr->Clone(true, false, 1);
    cloned->Node(cloned->Size() - 1) = size;
    InsertProperty(cloned, da->Evaluate(3));
    cloned->Release();
    return size;
}
