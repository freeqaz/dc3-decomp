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

    // Character animation pipeline diagnostics
    bool clipDirOk = false;
    bool masterClipOk = false;
    int charClipLayers = -1; // -1 = no character, 0 = char exists but no clips
    bool player0Ok = false, player1Ok = false;
    bool clipPlayerInited = false;
    int diffProxyExists = 0;
    int songAnimKeyTotal = -1;  // -1 = no anim, 0+ = PropKeys track count
    int clipKeyCount = -1;      // -1 = no "clip" PropKeys, 0+ = keyframe count
    int routineLoaded = 0;
    int mergeMoves = -1;
    int p0SongAnim = -99;       // HamCharacter::SongAnimation() for player 0
    int doSongAnim = -1;        // HamDirector::SongAnimation() equivalent
    if (TheHamDirector) {
        clipDirOk = TheHamDirector->ClipDir() != nullptr;
        PropKeys *mk = TheHamDirector->GetMasterKeys("clip");
        masterClipOk = mk != nullptr;

        // Test if ClipPlayer::Init(0) would succeed
        ClipPlayer testPlayer;
        clipPlayerInited = testPlayer.Init(0);

        // Difficulty proxy: does "easy" ObjectDir exist in the merged world?
        // GetPropAnim is public and internally calls GetDifficultyProxy
        RndPropAnim *easyAnim = TheHamDirector->GetPropAnim(kDifficultyEasy, "song.anim", false);
        if (easyAnim) diffProxyExists = 1;

        // PropKeys detail on the difficulty song.anim
        if (easyAnim) {
            // Count how many known PropKeys tracks exist
            songAnimKeyTotal = 0;
            const char *propNames[] = {"clip", "move", "practice"};
            for (int pi = 0; pi < 3; pi++) {
                PropKeys *pk = easyAnim->GetKeys(TheHamDirector, DataArrayPtr(Symbol(propNames[pi])));
                if (pk) songAnimKeyTotal++;
            }
        }

        // The key diagnostic: how many keyframes does the "clip" PropKeys have?
        PropKeys *ck = TheHamDirector->GetPropKeys(kDifficultyEasy, Symbol("clip"));
        if (ck) {
            clipKeyCount = ck->NumKeys();
        } else {
            clipKeyCount = 0;
        }

        // Routine loaded: confirms MoveMgr::SongInit → LoadRoutineBuilderData completed
        if (TheMoveMgr && TheHamProvider) {
            routineLoaded = TheMoveMgr->HasRoutine() ? 1 : 0;
            mergeMoves = TheHamProvider->Property("merge_moves", true)->Int();
        }

        // Check if player 0's character has clips queued via HamDriver
        if (TheHamWardrobe) {
            HamCharacter *ch0 = TheHamWardrobe->GetCharacter(0);
            HamCharacter *ch1 = TheHamWardrobe->GetCharacter(1);
            player0Ok = ch0 != nullptr;
            player1Ok = ch1 != nullptr;
            if (ch0) {
                charClipLayers = 0;
                HamDriver *drv = ch0->SongDriver();
                if (drv) {
                    charClipLayers = (int)drv->Layers().mLayers.size();
                }
                // SongAnimation gate: this must return > -1 for clip playback
                p0SongAnim = ch0->SongAnimation();
            }
            // Replicate HamDirector::SongAnimation() (protected)
            doSongAnim = 0;
            for (int ci = 0; ci < 2; ci++) {
                HamCharacter *hc = TheHamWardrobe->GetCharacter(ci);
                if (hc && hc->SongAnimation() > -1) {
                    doSongAnim = 1;
                    break;
                }
            }
        }
    }

    fprintf(stderr,
        "DC3_TEL: frame=%d state=%s beat=%.2f realSecs=%.2f "
        "songAnimFrame=%.1f pollEnabled=%d "
        "typeDef=%s hamProvider=%d mergerDir=%d "
        "clipDir=%d masterClip=%d clipPlayerInit=%d charClipLayers=%d p0=%d p1=%d "
        "clipKeyCount=%d songAnimKeys=%d diffProxy=%d routineLoaded=%d mergeMoves=%d "
        "p0SongAnim=%d doSongAnim=%d\n",
        frame, state, beat, realSecs,
        songAnimFrame, pollEnabled ? 1 : 0,
        typeDef, hamProvider ? 1 : 0, mergerDir ? 1 : 0,
        clipDirOk ? 1 : 0, masterClipOk ? 1 : 0, clipPlayerInited ? 1 : 0,
        charClipLayers, player0Ok ? 1 : 0, player1Ok ? 1 : 0,
        clipKeyCount, songAnimKeyTotal, diffProxyExists, routineLoaded, mergeMoves,
        p0SongAnim, doSongAnim
    );
}
