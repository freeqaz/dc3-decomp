#include "obj/Msg.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "utl/Str.h"
#include "utl/Symbol.h"
#include "world/CameraShot.h"

Symbol MsgSinks::sCurrentExportEvent(gNullStr);

Symbol MsgSinks::GetPropSyncHandler(DataArray *arr) {
    if (mPropSyncHandlers) {
        auto _tmp0 = mPropSyncHandlers->Size();
        for (int i = 0; i < _tmp0; i += 2) {
            DataArray *array = mPropSyncHandlers->Array(i);
            if (array->Size() == arr->Size()) {
                bool ret = true;
                for (int j = 0; j < array->Size(); j++) {
                    if (array->UncheckedInt(j) != arr->UncheckedInt(j)) {
                        ret = false;
                        break;
                    }
                }
                if (ret)
                    return mPropSyncHandlers->Sym(0);
            }
        }
    }
    return 0;
}

Symbol PathToEventName(DataArray *arr) {
    StackString<100> str("on_");
    str += arr->Sym(0).Str();
    for (int i = 1; i < arr->Size(); i++) {
        if (arr->Type(i) == kDataSymbol) {
            str += arr->LiteralSym(i).Str();
        } else {
            str += MakeString("%i", (CamShotFrame::BlendEaseMode)arr->Int(i));
        }
    }
    str += "_change";
    return str.c_str();
}

bool MsgSinks::HasPropertySink(Hmx::Object *o, DataArray *a) {
    Symbol path = PathToEventName(a);
    if (mPropSyncHandlers) {
        for (int i = 1; i < mPropSyncHandlers->Size(); i += 2) {
            if (path == mPropSyncHandlers->Sym(i)) {
                return true;
            }
        }
    }
    return false;
}

bool MsgSinks::HasSink(Hmx::Object *o) const {
    for (ObjList<Sink>::const_iterator it = mSinks.begin(); it != mSinks.end(); ++it) {
        if (it->obj == o) {
            return true;
        }
    }
    return false;
}

MsgSinks::EventSinkElem &
MsgSinks::EventSinkElem::operator=(const EventSinkElem &other) {
    Sink::operator=(other);
    handler = other.handler;
    return *this;
}

void MsgSinks::ChainEventSinks(Hmx::Object *from, Hmx::Object *to) {
    for (ObjList<EventSink>::const_iterator it = mEventSinks.begin();
         it != mEventSinks.end();
         ++it) {
        if (it->chainProxy) {
            from->AddSink(to, it->event);
        }
    }
}

void MsgSinks::EventSink::Remove(Hmx::Object *o, bool exporting) {
    for (ObjList<EventSinkElem>::iterator it = sinks.begin(); it != sinks.end(); ++it) {
        if (it->obj == o) {
            it->obj = nullptr;
            // When exporting, null the pointer but keep the element in the list
            if (exporting) {
                return;
            }
            sinks.erase(it);
            return;
        }
    }
}

void MsgSinks::EventSink::Add(
    Hmx::Object *obj, Hmx::Object::SinkMode mode, Symbol s, bool b4
) {
    EventSinkElem elem(sinks.Owner());
    elem.obj.SetObjConcrete(obj);
    elem.mode = mode;
    elem.handler = s;
    if (b4) {
        sinks.push_front(elem);
    } else {
        sinks.push_back(elem);
    }
}

MsgSinks::~MsgSinks() {
    if (mPropSyncHandlers)
        mPropSyncHandlers->Release();
}

MsgSinks::MsgSinks(Hmx::Object *o)
    : mPropSyncHandlers(nullptr), mSinks(o), mEventSinks(o), mExporting(0), mOwner(o) {}

// BEGIN_CUSTOM_PROPSYNC(MsgSinks::Sink)
//     SYNC_PROP(obj, (Hmx::Object *&)o.obj)
//     SYNC_PROP(mode, (int &)o.mode)
// END_CUSTOM_PROPSYNC

// BEGIN_CUSTOM_PROPSYNC(MsgSinks)
//     SYNC_PROP(sinks, o.mSinks)
//     SYNC_PROP(event_sinks, o.mEventSinks)
// END_PROPSYNCS

void MsgSinks::AddSink(
    Hmx::Object *s, Symbol ev, Symbol handler, Hmx::Object::SinkMode mode, bool chainProxy
) {
    MILO_ASSERT(s, 0x9C);
    if (ev.Null() && !chainProxy) {
        MILO_NOTIFY("%s can't have chainProxy false with no event", PathName(mOwner));
    }
    RemoveSink(s, ev);
    if (ev.Null()) {
        MILO_ASSERT(s != mOwner, 0xA6);
        Sink sink(mOwner);
        sink.obj.SetObjConcrete(s);
        sink.mode = mode;
        if (mExporting != 0) {
            mSinks.push_front(sink);
        } else {
            mSinks.push_back(sink);
        }
    } else {
        if (handler.Null())
            handler = ev;
        MILO_ASSERT((s != mOwner) || (handler != ev), 0xB9);
        ObjList<EventSink>::iterator found;
        for (found = mEventSinks.begin(); found != mEventSinks.end(); ++found) {
            if (found->event == ev) {
                if (chainProxy != found->chainProxy) {
                    MILO_NOTIFY("%s mismatched proxy chain for %s", PathName(mOwner), ev);
                }
                found->Add(s, mode, handler, mExporting);
                return;
            }
        }
        mEventSinks.push_back();
        mEventSinks.back().event = ev;
        mEventSinks.back().chainProxy = chainProxy;
        mEventSinks.back().Add(s, mode, handler, mExporting);
    }
}

void MsgSinks::AddPropertySink(Hmx::Object *o, DataArray *a, Symbol s) {
    Symbol handler = GetPropSyncHandler(a);
    Symbol path = PathToEventName(a);
    if (!mPropSyncHandlers) {
        mPropSyncHandlers = new DataArray(2);
    } else {
        mPropSyncHandlers->Resize(mPropSyncHandlers->Size() + 2);
    }
    mPropSyncHandlers->Node(mPropSyncHandlers->Size() - 2) = DataNode(a->Clone(true, false, 0), kDataArray);
    mPropSyncHandlers->Node(mPropSyncHandlers->Size() - 2).LiteralArray()->Release();
    mPropSyncHandlers->Node(mPropSyncHandlers->Size() - 1) = path;
    AddSink(o, path, s, Hmx::Object::kHandle, false);
}

static void ExportSink(Hmx::Object *obj, Hmx::Object::SinkMode mode, DataArray *arr) {
    switch (mode) {
    case Hmx::Object::kHandle:
        obj->Handle(arr, false);
        break;
    case Hmx::Object::kExport:
        obj->Export(arr, false);
        break;
    case Hmx::Object::kType:
        obj->HandleType(arr);
        break;
    case Hmx::Object::kExportType:
        obj->Export(arr, true);
        break;
    }
}

void MsgSinks::Export(DataArray *arr) {
    mExporting++;
    // Dispatch to global sinks
    for (ObjList<Sink>::iterator it = mSinks.begin(); it != mSinks.end();) {
        if (!(it->obj == nullptr)) {
            ExportSink(it->obj, it->mode, arr);
        } else {
            if (mExporting == 1) {
                it = mSinks.erase(it);
                continue;
            }
        }
        ++it;
    }

    // Find and dispatch to event-specific sinks
    Symbol msgType = arr->Sym(1);
        for (ObjList<EventSink>::iterator evIt = mEventSinks.begin();
         evIt != mEventSinks.end(); ++evIt) {
        if (evIt->event == arr->Sym(1)) {
            // Save original message node and replace with handler symbol
            DataNode origNode = arr->Node(1);
            for (ObjList<EventSinkElem>::iterator sinkIt = evIt->sinks.begin();
                 sinkIt != evIt->sinks.end();) {
                if (sinkIt->obj == nullptr) {
                    if (mExporting == 1) {
                        sinkIt = evIt->sinks.erase(sinkIt);
                        continue;
                    }
                } else {
                    arr->Node(1) = DataNode(sinkIt->handler);
                    ExportSink(sinkIt->obj, sinkIt->mode, arr);
                }
                ++sinkIt;
            }
            // Restore original message node
            arr->Node(1) = origNode;
            break;
        }
    }

    sCurrentExportEvent = msgType = sCurrentExportEvent;
    mExporting--;
}

void MsgSinks::RemoveSink(Hmx::Object *obj, Symbol ev) {
    if (ev.Null()) {
        for (ObjList<Sink>::iterator it = mSinks.begin(); it != mSinks.end(); ++it) {
            if (it->obj == obj) {
                if (mExporting) {
                    it->obj = nullptr;
                } else {
                    mSinks.erase(it);
                }
                return;
            }
        }
    } else {
        for (ObjList<EventSink>::iterator it = mEventSinks.begin();
             it != mEventSinks.end(); ++it) {
            if (it->event == ev) {
                it->Remove(obj, mExporting != 0);
                if (it->sinks.empty() && !mExporting) {
                    mEventSinks.erase(it);
                }
                return;
            }
        }
    }
}

bool MsgSinks::Replace(ObjRef *ref, Hmx::Object *obj) {
    // Check global sinks
    for (ObjList<Sink>::iterator it = mSinks.begin(); it != mSinks.end(); ++it) {
        if (&it->obj == ref) {
            it->obj = static_cast<Hmx::Object*>(obj);
            return true;
        }
    }
    // Check event sinks
    for (ObjList<EventSink>::iterator evIt = mEventSinks.begin();
         evIt != mEventSinks.end(); ++evIt) {
        for (ObjList<EventSinkElem>::iterator sinkIt = evIt->sinks.begin();
             sinkIt != evIt->sinks.end(); ++sinkIt) {
            if (&sinkIt->obj == ref) {
                sinkIt->obj = static_cast<Hmx::Object*>(obj);
                return true;
            }
        }
    }
    return false;
}
