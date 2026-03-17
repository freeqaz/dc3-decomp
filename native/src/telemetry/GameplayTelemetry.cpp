#include "telemetry/GameplayTelemetry.h"
#include <cstdio>
#include <cstdlib>

#include "hamobj/HamDirector.h"
#include "obj/Task.h"
#include "rndobj/PropAnim.h"
#include "world/Dir.h"
#include "flow/PropertyEventProvider.h"

// Forward declarations for globals we read
extern HamDirector *TheHamDirector;
extern WorldDir *TheWorld;
extern PropertyEventProvider *TheHamProvider;

// GamePanel — use IsGameOver() and public accessors to avoid protected access
#include "lazer/game/GamePanel.h"
extern GamePanel *TheGamePanel;

namespace {
    bool sEnabled = false;
    int sInterval = 10; // emit every N frames
}

void GameplayTelemetry::Init() {
    const char *env = getenv("DC3_TEL");
    sEnabled = env && atoi(env) != 0;
    if (sEnabled) fprintf(stderr, "DC3_TEL: telemetry enabled (interval=%d)\n", sInterval);
    const char *intEnv = getenv("DC3_TEL_INTERVAL");
    if (intEnv) sInterval = atoi(intEnv);
    if (sInterval <= 0) sInterval = 10;
}

bool GameplayTelemetry::IsEnabled() {
    return sEnabled;
}

// GamePanel state detection using public API
static const char *GetGameState() {
    if (!TheGamePanel) return "boot";
    if (TheGamePanel->IsGameOver()) return "gameover";
    // IsLoaded() returns true once PollForLoading reaches state 4
    // After that, we're either in intro or playing
    // Use Unkf8() — returns true once StartIntro() has been called
    if (TheGamePanel->Unkf8()) return "playing";
    return "intro";
}

void GameplayTelemetry::Sample(int frame) {
    if (!sEnabled) return;
    if (frame % sInterval != 0) return;

    const char *state = GetGameState();

    // Beat and timing
    float beat = TheTaskMgr.Beat();
    float realSecs = TheTaskMgr.Seconds(TaskMgr::kRealTime);

    // Song anim frame
    float songAnimFrame = 0.0f;
    bool pollEnabled = false;
    if (TheHamDirector) {
        RndPropAnim *anim = TheHamDirector->SongAnim(0);
        if (anim) {
            songAnimFrame = anim->GetFrame();
            // Clamp garbage values from uninitialized prop anims
            if (songAnimFrame < -1e6f || songAnimFrame > 1e6f)
                songAnimFrame = 0.0f;
        }
        // mPollEnabled is protected; infer from whether Poll() output changes
        // If songAnimFrame > 0, polling must be enabled
        pollEnabled = (songAnimFrame > 0.0f);
    }

    // Venue TypeDef
    const char *typeDef = "";
    WorldDir *venue = TheHamDirector ? TheHamDirector->GetVenueWorld() : nullptr;
    if (venue && venue->TypeDef()) {
        typeDef = venue->Type().Str();
    }

    // HamProvider
    bool hamProvider = TheHamProvider != nullptr;

    // MergerDir
    bool mergerDir = false;
    if (TheHamDirector && TheHamDirector->GetMerger()) {
        mergerDir = TheHamDirector->MergerDir() != nullptr;
    }

    fprintf(stderr,
        "DC3_TEL: frame=%d state=%s beat=%.2f realSecs=%.2f "
        "songAnimFrame=%.1f pollEnabled=%d "
        "typeDef=%s hamProvider=%d mergerDir=%d\n",
        frame, state, beat, realSecs,
        songAnimFrame, pollEnabled ? 1 : 0,
        typeDef, hamProvider ? 1 : 0, mergerDir ? 1 : 0
    );
}
