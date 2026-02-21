#include "App.h"
#include "ChecksumData_xbox.h"
#include "game/Game.h"
#include "gesture/GestureMgr.h"
#include "gesture/SkeletonUpdate.h"
#include "hamobj/HamNavList.h"
#include "obj/Dir.h"
#include "os/Debug.h"
#include "os/Timer.h"
#include "rndobj/HiResScreen.h"
#include "rndobj/Rnd.h"
#include "ui/UI.h"
#include <cstring>

App::App(int, char **) {
    ObjDirPtr<ObjectDir> dPtr;
    AutoTimer timer(0, 0, 0, 0);
}

void App::CaptureHiRes() {
    bool paused = AllPaused();

    if (paused)
        TheGame->SetTimePaused(true);

    DrawRegular();

    int tiles = TheHiResScreen.GetTiling() * TheHiResScreen.GetTiling();

    for (int i = 0; i <= tiles; i++) {
        DrawRegular();
        TheHiResScreen.Accumulate();
    }

    TheHiResScreen.Finish();

    if (paused)
        TheGame->SetTimePaused(false);
}

void App::DrawRegular() {
    TheRnd.BeginDrawing();
    TheUI->Draw();
    TheRnd.EndDrawing();
}

App::~App() { TheDebug.Exit(0, true); }

bool XShowNuiCallback(u32 &p1) {
    bool ret;

    MILO_ASSERT(TheGestureMgr, 0x87);

    Skeleton *skel = TheGestureMgr->GetActiveSkeleton();

    if (!HamNavList::sLastSelectInControllerMode && skel && skel->IsTracked()) {
        ret = true;
        p1 = skel->TrackingID();
    } else {
        ret = false;
    }

    return ret;

}

bool EndsWith(const char *str, const char *suffix) {
    int strLen = strlen(str);
    int suffixLen = strlen(suffix);
    return strstr(str, suffix) == str + strLen - suffixLen;
}

namespace {
    bool gListenForKinectGuide;
}

DWORD KinectGuideThread(void *) {
    HRESULT hr;
    hr = NuiSkeletonTrackingDisable();
    if (hr < 0) {
        MILO_FAIL("NuiSkeletonTrackingDisable failed");
    }
    hr = NuiSkeletonTrackingEnable(0, 0);
    if (hr < 0) {
        MILO_FAIL("NuiSkeletonTrackingEnable failed");
    }
    HANDLE kinect_listener = XNotifyCreateListener(1);
    MILO_ASSERT(kinect_listener, 0xa2);
    while (gListenForKinectGuide) {
        DWORD dwId;
        ULONG_PTR param;
        while (XNotifyGetNext(kinect_listener, 0, &dwId, &param)) {
            if (dwId == 0x6001a) {
                XShowNuiGuideUI(param);
            }
        }
    }
    CloseHandle(kinect_listener);
    hr = NuiSkeletonTrackingDisable();
    if (hr < 0) {
        MILO_FAIL("NuiSkeletonTrackingDisable failed");
    }
    hr = NuiSkeletonTrackingEnable(SkeletonUpdate::NewSkeletonEvent(), 2);
    if (hr < 0) {
        MILO_FAIL("NuiSkeletonTrackingEnable failed");
    }
    return 0;
}
