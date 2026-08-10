#pragma once
#include "obj\Data.h"
#include "obj\Object.h"
#include "ui\UIPanel.h"

class DepthBuffer : public Hmx::Object {
public:
    enum State {
        kDepthBuffer_Normal = 0,
        kDepthBuffer_Tutorial = 1,
        kDepthBufferGameplay_Perform = 10,
        kDepthBufferGameplay_Practice_Learn = 11,
        kDepthBufferGameplay_Practice_Review = 12,
    };
    DepthBuffer();
    virtual ~DepthBuffer() {}
    virtual DataNode Handle(DataArray *, bool);

    void Init(UIPanel *);
    void Poll();

    void SetState(int state) {
        if (state != mState) {
            mState = (State)state;
        }
    }

private:
    UIPanel *mPanel; // 0x2c
    State mState; // 0x30
    bool mNeedsRedraw;
};
