#include "meta_ham/UIEventMgr.h"
#include "macros.h"
#include "meta_ham/EventDialogPanel.h"
#include "meta_ham/HamUI.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/System.h"
#include "ui/UI.h"
#include "ui/UIScreen.h"
#include "utl/Std.h"
#include "utl/Symbol.h"

UIEventMgr *TheUIEventMgr;

UIEventMgr::BandEvent::BandEvent(UIEventMgr::EventType type, DataArray *eventDef, DataArray *eventData)
    : mType(type), mDataArray(eventDef), mActive(false) {
    if (eventData) {
        mEventParams = eventData->Clone(true, true, 0);
    }
}

void UIEventMgr::BandEvent::CacheDestination() {
    mDataArray->FindData("destination_screen", mDestScreen, false);
}

UIEventMgr::UIEventMgr() {}
UIEventMgr::~UIEventMgr() { DeleteAll(mEventQueue); }

BEGIN_HANDLERS(UIEventMgr)
    HANDLE(trigger_event, OnTriggerEvent)
    HANDLE_ACTION(dismiss_event, DismissEvent(gNullStr))
    HANDLE_EXPR(has_active_transition_event, HasActiveTransitionEvent())
    HANDLE_EXPR(has_active_dialog_event, HasActiveDialogEvent())
    HANDLE_EXPR(current_event, CurrentEvent())
    HANDLE_EXPR(is_transition_event_started, IsTransitionEventStarted())
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

bool UIEventMgr::HasActiveTransitionEvent() const {
    if (mEventQueue.empty())
        return false;
    else
        return mEventQueue[0]->mType == kTransitionEvent;
}

bool UIEventMgr::HasActiveDialogEvent() const {
    if (mEventQueue.empty())
        return false;
    else
        return mEventQueue[0]->mType == kDialogEvent;
}

void UIEventMgr::Init() {
    MILO_ASSERT(!TheUIEventMgr, 0x14);
    TheUIEventMgr = new UIEventMgr();
    TheUIEventMgr->SetName("ui_event_mgr", ObjectDir::Main());
    TheUIEventMgr->SetTypeDef(SystemConfig("ui", "ui_event_mgr"));
}

void UIEventMgr::Terminate() { RELEASE(TheUIEventMgr); }

Symbol UIEventMgr::CurrentEvent() const {
    if (!mEventQueue.empty()) {
        return mEventQueue[0]->mDataArray->Sym(0);
    }
    return gNullStr;
}

bool UIEventMgr::IsTransitionEventStarted() const {
    MILO_ASSERT(HasActiveTransitionEvent(), 0xDC);
    static Symbol next_screen("next_screen");
    const char *nextScreen = mEventQueue[0]->mDataArray->FindStr(next_screen);
    const char *bottomScreen;
    if (TheUI->BottomScreen()) {
        bottomScreen = TheUI->BottomScreen()->Name();
    } else {
        bottomScreen = "";
    }
    return streq(nextScreen, bottomScreen);
}

bool UIEventMgr::IsTransitionEventFinished() const {
    MILO_ASSERT(HasActiveTransitionEvent(), 0xD1);
    const char *cc = mEventQueue[0]->mDestScreen.c_str();
    const char *curScreen;
    if (TheUI->CurrentScreen()) {
        curScreen = TheUI->CurrentScreen()->Name();
    } else {
        curScreen = "";
    }
    return streq(cc, curScreen);
}

void UIEventMgr::ActivateFirstEvent() {
    MILO_ASSERT(mEventQueue.size(), 0x89);
    BandEvent *firstEvent = mEventQueue[0];
    MILO_ASSERT(!firstEvent->mActive, 0x8B);
    firstEvent->mActive = true;
    if (firstEvent->mType == kTransitionEvent) {
        DataArray *initArr = firstEvent->mDataArray->FindArray("init", false);
        if (initArr) {
            initArr->ExecuteScript(1, nullptr, firstEvent->mEventParams, 2);
        }
        firstEvent->CacheDestination();
        static Symbol next_screen("next_screen");
        TheHamUI.GotoEventScreen(
            ObjectDir::Main()->Find<UIScreen>(firstEvent->mDataArray->FindStr(next_screen))
        );
    } else if (firstEvent->mType == kDialogEvent) {
        static Message init_msg("init");
        static EventDialogStartMsg msg(firstEvent->mDataArray, init_msg);
        msg[0] = firstEvent->mDataArray;
        if (firstEvent->mEventParams->Size() == 0) {
            firstEvent->mEventParams = init_msg;
        }
        msg[1] = firstEvent->mEventParams;
        Export(msg, false);
    }
}

void UIEventMgr::DismissEvent(Symbol dismissReason) {
    MILO_ASSERT(!mEventQueue.empty(), 0x2D);
    MILO_ASSERT(mEventQueue.front()->mActive, 0x2E);
    Symbol curEvent = CurrentEvent();
    EventType eventType = mEventQueue.front()->mType;
    delete mEventQueue.front();
    mEventQueue.erase(mEventQueue.begin());
    if (eventType == kDialogEvent) {
        static EventDialogDismissMsg dismiss_msg(gNullStr, gNullStr);
        dismiss_msg[0] = curEvent;
        dismiss_msg[1] = dismissReason;
        Export(dismiss_msg, false);
    }
    if (mEventQueue.size() > 0 && !mEventQueue.front()->mActive) {
        ActivateFirstEvent();
    }
}

void UIEventMgr::TriggerEvent(Symbol eventName, DataArray *eventData) {
    // Check if current screen allows this event
    if (!TheUI->InTransition()) {
        UIScreen *curScreen = TheUI->CurrentScreen();
        if (TheUI->CurrentScreen()) {
            static Message msg("allow_event", 0);
            msg[0] = eventName;
            DataNode handled = curScreen->HandleType(msg);
            if (handled.Type() != kDataUnhandled && handled.Int() == 0) {
                return;
            }
        }
    }

    // Dismiss pending dialog events (keep transition events)
    while (!mEventQueue.empty()) {
        BandEvent *evt = mEventQueue.back();
        if (evt->mType) {
            break;
        }
        if (mEventQueue.size() == 1 && evt->mActive) {
            DismissEvent(eventName);
        } else {
            RELEASE(evt);
            mEventQueue.pop_back();
        }
    }

    // Look up event definition from typedef
    static Symbol dialog_events("dialog_events");
    static Symbol transition_events("transition_events");
    DataArray *dialogArr = TypeDef()->FindArray(dialog_events);
    DataArray *eventArr = dialogArr->FindArray(eventName, false);
    EventType eventType;
    if (eventArr) {
        eventType = kDialogEvent;
    } else {
        eventArr = TypeDef()->FindArray(transition_events, eventName);
        eventType = kTransitionEvent;
    }
    mEventQueue.push_back(new BandEvent(eventType, eventArr, eventData));
    if (mEventQueue.size() == 1) {
        ActivateFirstEvent();
    }
}

DataNode UIEventMgr::OnTriggerEvent(DataArray *msg) {
    Symbol eventName = msg->Sym(2);
    DataArray *eventData = nullptr;
    if (msg->Size() > 3) {
        eventData = msg->Array(3);
    }
    TriggerEvent(eventName, eventData);
    return 1;
}
