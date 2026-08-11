#pragma once
#include "obj\Data.h"
#include "obj\Msg.h"
#include "obj/Object.h"
#include "utl\Symbol.h"

class PresenceMgr : public Hmx::Object {
public:
    PresenceMgr()
        : mPresenceModes(0), mPresenceModeContexts(0), mInstrumentPlayModeContexts(0),
          mSongID(0), mInGame(0) {}
    virtual DataNode Handle(DataArray *, bool);

    void Init();
    void SetNotInGame();
    void SetInGame(int);

protected:
    bool IsPadPlaying(int);
    Symbol GetPresenceMode();
    int GetPresenceContextFromMode(Symbol, bool);
    int GetPlayModeContext();
    void UpdatePresence();
    DataNode OnPlayerPresentChange(DataArray *);
    DataNode OnPresenceChange(DataArray *);

    DataArray *mPresenceModes; // 0x2c
    DataArray *mPresenceModeContexts; // 0x30
    DataArray *mInstrumentPlayModeContexts; // 0x34
    Symbol unk38; // 0x38
    int mSongID; // 0x3c
    bool mInGame; // 0x40
};

extern PresenceMgr ThePresenceMgr;

DECLARE_MESSAGE(CurrentScreenChangedMsg, "current_screen_changed")
CurrentScreenChangedMsg(Symbol s) : Message(Type(), s) {}
END_MESSAGE
