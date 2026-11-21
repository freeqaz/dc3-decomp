// DC3 Native Port - the cursor panel's two player boxes when there is no
// camera.  PlayerBox_Native.h has the what and the why.

#include "platform/PlayerBox_Native.h"
#include "platform/CameraStream_Native.h"
#include "obj/Dir.h"
#include "rndobj/Draw.h"

#include <cstdio>
#include <cstdlib>
#include <strings.h>

namespace NativePlayerBox {

const char *const kGroupNames[2] = { "depth_buffer_left.grp", "depth_buffer_right.grp" };

namespace {

bool sModeRead = false;
Mode sMode = kAuto;
// Cursor-panel draws since the start (or ResetForTest), by what they did.
unsigned long sDrawsShown = 0;
unsigned long sDrawsHidden = 0;
int sGroupsHiddenLastDraw = 0;

bool IsWord(const char *text, const char *const *words) {
    for (; *words; words++) {
        if (!strcasecmp(text, *words))
            return true;
    }
    return false;
}

} // namespace

bool ParseMode(const char *text, Mode *mode) {
    static const char *const kAutoWords[] = { "auto", nullptr };
    static const char *const kOnWords[] = { "on", "1", "true", "yes", "always", nullptr };
    static const char *const kOffWords[] = { "off", "0", "false", "no", "never", nullptr };
    if (!text || !*text)
        return false;
    if (IsWord(text, kAutoWords))
        *mode = kAuto;
    else if (IsWord(text, kOnWords))
        *mode = kOn;
    else if (IsWord(text, kOffWords))
        *mode = kOff;
    else
        return false;
    return true;
}

const char *ModeName(Mode mode) {
    switch (mode) {
    case kOn:
        return "on";
    case kOff:
        return "off";
    default:
        return "auto";
    }
}

bool ShouldDraw(Mode mode, bool cameraLive) {
    switch (mode) {
    case kOn:
        return true;
    case kOff:
        return false;
    default:
        return cameraLive;
    }
}

Mode ModeFromEnv(const char *value, bool *unknown) {
    Mode mode = kAuto;
    bool known = !value || !*value || ParseMode(value, &mode);
    if (unknown)
        *unknown = !known;
    return mode;
}

Mode GetMode() {
    if (!sModeRead) {
        sModeRead = true;
        const char *value = getenv("DC3_PLAYER_BOX");
        bool unknown = false;
        sMode = ModeFromEnv(value, &unknown);
        if (unknown) {
            fprintf(stderr,
                    "[PlayerBox] DC3_PLAYER_BOX=%s is not auto, on or off; using auto\n",
                    value);
        }
    }
    return sMode;
}

void SetMode(Mode mode) {
    sModeRead = true;
    sMode = mode;
}

bool DrawnNow() { return ShouldDraw(GetMode(), NativeCamera::Active()); }

DrawScope::DrawScope(ObjectDir *cursorDir) : mCount(0) {
    mHidden[0] = mHidden[1] = nullptr;
    if (DrawnNow()) {
        sDrawsShown++;
        sGroupsHiddenLastDraw = 0;
        return;
    }
    if (cursorDir) {
        for (int i = 0; i < 2; i++) {
            RndDrawable *group = cursorDir->Find<RndDrawable>(kGroupNames[i], false);
            if (group && group->Showing()) {
                group->SetShowing(false);
                mHidden[mCount++] = group;
            }
        }
    }
    sDrawsHidden++;
    sGroupsHiddenLastDraw = mCount;
}

DrawScope::~DrawScope() {
    for (int i = 0; i < mCount; i++)
        mHidden[i]->SetShowing(true);
}

std::string StateJson() {
    std::string j = "{\"mode\":\"";
    j += ModeName(GetMode());
    j += "\",\"drawn\":";
    j += DrawnNow() ? "true" : "false";
    j += ",\"drawsShown\":" + std::to_string(sDrawsShown);
    j += ",\"drawsHidden\":" + std::to_string(sDrawsHidden);
    j += ",\"groupsHidden\":" + std::to_string(sGroupsHiddenLastDraw);
    j += "}";
    return j;
}

void ResetForTest() {
    sModeRead = false;
    sMode = kAuto;
    sDrawsShown = sDrawsHidden = 0;
    sGroupsHiddenLastDraw = 0;
}

} // namespace NativePlayerBox
