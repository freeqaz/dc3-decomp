#include "telemetry/GameplayTelemetry.h"
#include <cstdio>
#include <cstdlib>

#include "hamobj/HamDirector.h"
#include "hamobj/HamWardrobe.h"
#include "hamobj/HamCharacter.h"
#include "hamobj/HamDriver.h"
#include "hamobj/ClipPlayer.h"
#include "hamobj/MoveMgr.h"
#include "obj/Task.h"
#include "rndobj/PropAnim.h"
#include "rndobj/PropKeys.h"
#include "world/Dir.h"
#include "flow/PropertyEventProvider.h"

// Forward declarations for globals we read
extern HamDirector *TheHamDirector;
extern HamWardrobe *TheHamWardrobe;
extern WorldDir *TheWorld;
extern PropertyEventProvider *TheHamProvider;

// GamePanel — use IsGameOver() and public accessors to avoid protected access
#include "lazer/game/GamePanel.h"
extern GamePanel *TheGamePanel;

namespace {
    bool sEnabled = false;
    int sInterval = 10; // emit every N frames
    GameplayTelemetry::Snapshot sLastSnapshot;
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
    if (TheGamePanel->Unkf8()) return "playing";
    return "intro";
}

GameplayTelemetry::Snapshot GameplayTelemetry::CaptureSnapshot(int frame) {
    Snapshot s;
    s.frame = frame;
    s.state = GetGameState();

    // Beat and timing
    s.beat = TheTaskMgr.Beat();
    s.realSecs = TheTaskMgr.Seconds(TaskMgr::kRealTime);

    // Song anim frame
    if (TheHamDirector) {
        RndPropAnim *anim = TheHamDirector->SongAnim(0);
        if (anim) {
            s.songAnimFrame = anim->GetFrame();
            if (s.songAnimFrame < -1e6f || s.songAnimFrame > 1e6f)
                s.songAnimFrame = 0.0f;
        }
        s.pollEnabled = (s.songAnimFrame > 0.0f);
    }

    // Venue TypeDef
    WorldDir *venue = TheHamDirector ? TheHamDirector->GetVenueWorld() : nullptr;
    if (venue && venue->TypeDef()) {
        s.typeDef = venue->Type().Str();
    }

    // HamProvider
    s.hamProvider = TheHamProvider != nullptr;

    // MergerDir
    if (TheHamDirector && TheHamDirector->GetMerger()) {
        s.mergerDir = TheHamDirector->MergerDir() != nullptr;
    }

    // Character animation pipeline diagnostics
    if (TheHamDirector) {
        s.clipDir = TheHamDirector->ClipDir() != nullptr;
        PropKeys *mk = TheHamDirector->GetMasterKeys("clip");
        s.masterClip = mk != nullptr;

        ClipPlayer testPlayer;
        s.clipPlayerInit = testPlayer.Init(0);

        RndPropAnim *easyAnim = TheHamDirector->GetPropAnim(kDifficultyEasy, "song.anim", false);
        if (easyAnim) s.diffProxy = 1;

        if (easyAnim) {
            s.songAnimKeys = 0;
            const char *propNames[] = {"clip", "move", "practice"};
            for (int pi = 0; pi < 3; pi++) {
                PropKeys *pk = easyAnim->GetKeys(TheHamDirector, DataArrayPtr(Symbol(propNames[pi])));
                if (pk) s.songAnimKeys++;
            }
        }

        PropKeys *ck = TheHamDirector->GetPropKeys(kDifficultyEasy, Symbol("clip"));
        if (ck) {
            s.clipKeyCount = ck->NumKeys();
        } else {
            s.clipKeyCount = 0;
        }

        if (TheMoveMgr && TheHamProvider) {
            s.routineLoaded = TheMoveMgr->HasRoutine() ? 1 : 0;
            s.mergeMoves = TheHamProvider->Property("merge_moves", true)->Int();
        }

        if (TheHamWardrobe) {
            HamCharacter *ch0 = TheHamWardrobe->GetCharacter(0);
            HamCharacter *ch1 = TheHamWardrobe->GetCharacter(1);
            s.player0 = ch0 != nullptr;
            s.player1 = ch1 != nullptr;
            if (ch0) {
                s.charClipLayers = 0;
                HamDriver *drv = ch0->SongDriver();
                if (drv) {
                    s.charClipLayers = (int)drv->Layers().mLayers.size();
                }
                s.p0SongAnim = ch0->SongAnimation();
            }
            s.doSongAnim = 0;
            for (int ci = 0; ci < 2; ci++) {
                HamCharacter *hc = TheHamWardrobe->GetCharacter(ci);
                if (hc && hc->SongAnimation() > -1) {
                    s.doSongAnim = 1;
                    break;
                }
            }
        }
    }

    return s;
}

const GameplayTelemetry::Snapshot& GameplayTelemetry::LastSnapshot() {
    return sLastSnapshot;
}

void GameplayTelemetry::Sample(int frame) {
    if (!sEnabled) return;
    if (frame % sInterval != 0) return;

    Snapshot s = CaptureSnapshot(frame);
    sLastSnapshot = s;

    fprintf(stderr,
        "DC3_TEL: frame=%d state=%s beat=%.2f realSecs=%.2f "
        "songAnimFrame=%.1f pollEnabled=%d "
        "typeDef=%s hamProvider=%d mergerDir=%d "
        "clipDir=%d masterClip=%d clipPlayerInit=%d charClipLayers=%d p0=%d p1=%d "
        "clipKeyCount=%d songAnimKeys=%d diffProxy=%d routineLoaded=%d mergeMoves=%d "
        "p0SongAnim=%d doSongAnim=%d\n",
        s.frame, s.state, s.beat, s.realSecs,
        s.songAnimFrame, s.pollEnabled ? 1 : 0,
        s.typeDef, s.hamProvider ? 1 : 0, s.mergerDir ? 1 : 0,
        s.clipDir ? 1 : 0, s.masterClip ? 1 : 0, s.clipPlayerInit ? 1 : 0,
        s.charClipLayers, s.player0 ? 1 : 0, s.player1 ? 1 : 0,
        s.clipKeyCount, s.songAnimKeys, s.diffProxy, s.routineLoaded, s.mergeMoves,
        s.p0SongAnim, s.doSongAnim
    );
}
