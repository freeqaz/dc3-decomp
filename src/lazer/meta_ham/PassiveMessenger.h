#pragma once
#include "obj/Data.h"
#include "obj/Object.h"
#include "obj/PropSync.h"
#include "utl/Symbol.h"
#include <list>

enum PassiveMessageType {
    kPassiveMessageGeneral = 0,
    kPassiveMessageFitness = 1,
    kPassiveMessageUnlock = 2,
    kPassiveMessageServer = 3,
    kPassiveMessageCampaign = 4,
    kPassiveMessageFree4All = 5
};

class PassiveMessage {
public:
    PassiveMessage(String text, PassiveMessageType type, Symbol channel, int);
    virtual ~PassiveMessage();
    String GetText();
    PassiveMessageType Type() const { return mType; }
    Symbol Channel() const { return mChannel; }
    int Priority() const { return mPriority; }

private:
    String mText; // 0x4
    PassiveMessageType mType; // 0xc
    Symbol mChannel; // 0x10
    int mPriority; // 0x14
};

class PassiveMessageQueue {
public:
    PassiveMessageQueue(Hmx::Object *callback, Symbol);
    virtual ~PassiveMessageQueue();
    virtual PassiveMessage *GetAndPreProcessFirstMessage();
    virtual void AddMessage(PassiveMessage *);

    bool HasRecentlyDismissedMessage() const;
    bool RemoveLowerPriorityMessage(PassiveMessage *);
    void ClearRunningMessage();
    void Poll();

    int NumMessagesInQueue() const { return mQueue.size(); }
    bool Running() const { return mTimer.Running(); }

protected:
    void ClearPassiveMessage();
    void HandlePassiveMessage(PassiveMessage *);

    float mDisplayDurationMs;
    float mLastDismissTime; // 0xc
    std::list<PassiveMessage *> mQueue; // 0x10
    Hmx::Object *mCallback; // 0x18
    Symbol mPlayerChannel; // 0x1c
    Timer mTimer; // 0x20
};

class PassiveMessenger : public Hmx::Object {
public:
    PassiveMessenger(Hmx::Object *callback);
    virtual ~PassiveMessenger();
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);

    bool HasRecentlyDismissedMessage() const;
    void TriggerGenericMsg(Symbol, Symbol, PassiveMessageType, Symbol, int);
    void TriggerStringMsg(String, Symbol, PassiveMessageType, Symbol, int);
    void Poll();
    bool HasMessages() const;

private:
    bool AreSideMessagesAllowed() const;
    bool AreGlobalMessagesAllowed() const;
    void TriggerMessage(String, PassiveMessageType, Symbol, Symbol, int);

    PassiveMessageQueue *mMessageQueueP1; // 0x2c
    PassiveMessageQueue *mMessageQueueP2; // 0x30
    PassiveMessageQueue *mMessageQueueNone; // 0x34
    bool mEnabled; // 0x38
};

extern PassiveMessenger *ThePassiveMessenger;
