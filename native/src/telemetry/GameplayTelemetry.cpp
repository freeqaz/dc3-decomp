#include "telemetry/GameplayTelemetry.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hamobj/HamDirector.h"
#include "hamobj/HamWardrobe.h"
#include "hamobj/HamCharacter.h"
#include "hamobj/HamDriver.h"
#include "hamobj/ClipPlayer.h"
#include "hamobj/MoveMgr.h"
#include "char/FileMerger.h"
#include "char/CharUtl.h"
#include "obj/Dir.h"
#include "obj/Data.h"
#include "obj/Task.h"
#include "rndobj/Trans.h"
#include "rndobj/PropAnim.h"
#include "rndobj/PropKeys.h"
#include "ui/UI.h"
#include "ui/UIScreen.h"
#include "ui/UIPanel.h"
#include "world/Dir.h"
#include "flow/PropertyEventProvider.h"

// Forward declarations for globals we read
extern HamDirector *TheHamDirector;
extern HamWardrobe *TheHamWardrobe;
extern WorldDir *TheWorld;
extern PropertyEventProvider *TheHamProvider;

// Telemetry counters from engine code
extern int HamDirector_NativeSetFrameCount();

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

// GamePanel state detection — use C++ accessors directly.
// The previous code used HandleType() which only checks DTA type-def
// handlers. The is_playing/in_intro/is_game_over handlers are C++ HANDLE_EXPR
// macros in GamePanel::Handle(), unreachable via HandleType(). Using the
// public accessors (IsPlayingState, InIntroState, IsGameOver) is both correct
// and avoids the DTA dispatch overhead.
static const char *GetGameState() {
    if (!TheGamePanel) return "boot";

    if (TheGamePanel->IsGameOver()) return "gameover";
    if (TheGamePanel->IsPlayingState()) return "playing";
    if (TheGamePanel->InIntroState()) return "intro";

    return "loading";
}

GameplayTelemetry::Snapshot GameplayTelemetry::CaptureSnapshot(int frame) {
    Snapshot s;
    s.frame = frame;
    s.state = GetGameState();

    if (TheUI) {
        UIScreen *current = TheUI->CurrentScreen();
        UIScreen *transition = TheUI->TransitionScreen();
        s.screen = current ? current->Name() : "";
        s.transitionScreen = transition ? transition->Name() : "";
        s.uiInTransition = TheUI->InTransition();
        s.gameScreenActive = TheUI->IsGameScreenActive();

        UIPanel *worldPanel = ObjectDir::Main()->Find<UIPanel>("world_panel");
        if (worldPanel) {
            s.currentHasWorldPanel = current && current->HasPanel(worldPanel);
            s.transitionHasWorldPanel = transition && transition->HasPanel(worldPanel);
            s.worldPanelLoaded = worldPanel->IsLoaded();
        }
    }

    if (TheGamePanel) {
        s.gamePanelLoadState = TheGamePanel->PollLoadState();
        Game *game = TheGamePanel->CurrentGame();
        if (game) {
            s.gameWaitState = game->WaitState();
            s.gameLoadState = game->LoadState();
            s.gameUsesMoveGraph = game->UsesMoveGraph();
            s.gamePaused = game->Paused();
            s.gameRealTime = game->RealTime();
        }
    }

    // Beat and timing
    s.beat = TheTaskMgr.Beat();
    s.realSecs = TheTaskMgr.Seconds(TaskMgr::kRealTime);

    // Song anim frame
    if (TheHamDirector) {
        s.pollEnabled = TheHamDirector->PollEnabled();
        s.worldLoaded = TheHamDirector->IsWorldLoaded();
        s.worldPresent = TheHamDirector->GetWorld() != nullptr;
        s.venuePresent = TheHamDirector->GetVenueWorld() != nullptr;
        RndPropAnim *anim = TheHamDirector->SongAnim(0);
        if (anim) {
            s.songAnimFrame = anim->GetFrame();
            if (s.songAnimFrame < -1e6f || s.songAnimFrame > 1e6f)
                s.songAnimFrame = 0.0f;
        }
    }

    // Venue TypeDef
    WorldDir *venue = TheHamDirector ? TheHamDirector->GetVenueWorld() : nullptr;
    if (venue && venue->TypeDef()) {
        s.typeDef = venue->Type().Str();
    }

    // HamProvider
    s.hamProvider = TheHamProvider != nullptr;
    if (TheHamProvider) {
        s.gameStage = TheHamProvider->Property("game_stage", true)->Sym().Str();
    }

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

    // SetFrame path counter
    s.nativeSetFrameCount = HamDirector_NativeSetFrameCount();

    // Move/flashcard validation: check if move prop key track exists and has keys
    if (TheHamDirector) {
        PropKeys *moveKeys = TheHamDirector->GetPropKeys(kDifficultyEasy, Symbol("move"));
        if (moveKeys && moveKeys->NumKeys() > 0) {
            s.moveInterpActive = true;
            s.moveKeyCount = moveKeys->NumKeys();
        }
    }

    // Active move count: how many players have a real (non-null, non-Rest) current move
    if (TheGamePanel) {
        Game *game = TheGamePanel->CurrentGame();
        if (game) {
            MoveDir *moveDir = game->GetMoveDir();
            if (moveDir) {
                static Symbol restMove("Rest.move");
                for (int pi = 0; pi < 2; pi++) {
                    HamMove *curMove = moveDir->CurrentMove(pi);
                    if (curMove && strcmp(curMove->Name(), restMove.Str()) != 0) {
                        s.activeMoveCount++;
                    }
                }
            }
        }
    }

    // Song anim frame rate: delta from previous capture
    {
        static float sPrevSongAnimFrame = 0.0f;
        s.songAnimFrameRate = s.songAnimFrame - sPrevSongAnimFrame;
        sPrevSongAnimFrame = s.songAnimFrame;
    }

    // HUD merge convergence (T1-T4 invariants)
    if (TheHamDirector && TheHamDirector->GetGameModeMerger()) {
        FileMerger *fm = TheHamDirector->GetGameModeMerger();
        FileMerger::Merger *gm = fm->FindMerger("game_hud", false);
        if (gm) {
            s.hudMDirResolved = gm->mDir.Ptr() != nullptr;
            ObjectDir *mergeTarget = gm->MergerDir();
            WorldDir *world = TheHamDirector->GetWorld();
            if (world && world->GetHUD() && mergeTarget) {
                s.hudMergeTargetIsHUD = (mergeTarget == world->GetHUD());
                // T3: check if hud_left/hud_right are children of the merge target
                s.hudHasLeft = mergeTarget->Find<Hmx::Object>("hud_left", false) != nullptr;
                s.hudHasRight = mergeTarget->Find<Hmx::Object>("hud_right", false) != nullptr;

            }
            // T2: check $hud_panel identity
            if (world && world->GetHUD()) {
                DataNode &hpVar = DataVariable("hud_panel");
                if (hpVar.Type() == kDataObject) {
                    s.hudPanelIsHUD = (hpVar.GetObj() == world->GetHUD());
                }
            }
        }
    }

    // Foot orientation: find ankle and toe bones on player 0's character
    if (TheHamWardrobe) {
        HamCharacter *ch = TheHamWardrobe->GetCharacter(0);
        ObjectDir *charDir = ch ? (ObjectDir *)ch : nullptr;
        if (charDir) {
            RndTransformable *lAnkle = charDir->Find<RndTransformable>("bone_L-ankle.mesh", true);
            RndTransformable *rAnkle = charDir->Find<RndTransformable>("bone_R-ankle.mesh", true);
            RndTransformable *lToe = charDir->Find<RndTransformable>("bone_L-toe.mesh", true);
            RndTransformable *rToe = charDir->Find<RndTransformable>("bone_R-toe.mesh", true);
            if (lAnkle && rAnkle && lToe && rToe) {
                s.footDataValid = true;
                s.lAnkleZ = lAnkle->WorldXfm().v.z;
                s.lToeZ = lToe->WorldXfm().v.z;
                s.rAnkleZ = rAnkle->WorldXfm().v.z;
                s.rToeZ = rToe->WorldXfm().v.z;
                s.lFootZAxisZ = lAnkle->WorldXfm().m.z.z;
                s.rFootZAxisZ = rAnkle->WorldXfm().m.z.z;
                // Toe above ankle by >2 units = inverted
                s.lFootInverted = (s.lToeZ > s.lAnkleZ + 2.0f);
                s.rFootInverted = (s.rToeZ > s.rAnkleZ + 2.0f);
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
        "DC3_TEL: frame=%d state=%s screen=%s transition=%s uiInTransition=%d "
        "gameScreenActive=%d currentHasWorldPanel=%d transitionHasWorldPanel=%d "
        "worldPanelLoaded=%d gamePanelLoadState=%d gameWaitState=%d gameLoadState=%d "
        "gameUsesMoveGraph=%d gamePaused=%d gameRealTime=%d "
        "beat=%.2f realSecs=%.2f songAnimFrame=%.1f pollEnabled=%d "
        "worldLoaded=%d worldPresent=%d venuePresent=%d typeDef=%s gameStage=%s "
        "hamProvider=%d mergerDir=%d "
        "clipDir=%d masterClip=%d clipPlayerInit=%d charClipLayers=%d p0=%d p1=%d "
        "clipKeyCount=%d songAnimKeys=%d diffProxy=%d routineLoaded=%d mergeMoves=%d "
        "p0SongAnim=%d doSongAnim=%d nativeSetFrameCount=%d "
        "moveInterpActive=%d moveKeyCount=%d songAnimFrameRate=%.1f activeMoveCount=%d "
        "hudMergeTargetIsHUD=%d hudPanelIsHUD=%d hudHasLeft=%d hudHasRight=%d hudMDirResolved=%d "
        "footDataValid=%d lAnkleZ=%.1f lToeZ=%.1f rAnkleZ=%.1f rToeZ=%.1f "
        "lFootZAxisZ=%.2f rFootZAxisZ=%.2f lFootInverted=%d rFootInverted=%d\n",
        s.frame, s.state, s.screen, s.transitionScreen, s.uiInTransition ? 1 : 0,
        s.gameScreenActive ? 1 : 0, s.currentHasWorldPanel ? 1 : 0,
        s.transitionHasWorldPanel ? 1 : 0, s.worldPanelLoaded ? 1 : 0,
        s.gamePanelLoadState, s.gameWaitState, s.gameLoadState,
        s.gameUsesMoveGraph ? 1 : 0, s.gamePaused ? 1 : 0, s.gameRealTime ? 1 : 0,
        s.beat, s.realSecs, s.songAnimFrame, s.pollEnabled ? 1 : 0,
        s.worldLoaded ? 1 : 0, s.worldPresent ? 1 : 0, s.venuePresent ? 1 : 0,
        s.typeDef, s.gameStage,
        s.hamProvider ? 1 : 0, s.mergerDir ? 1 : 0,
        s.clipDir ? 1 : 0, s.masterClip ? 1 : 0, s.clipPlayerInit ? 1 : 0,
        s.charClipLayers, s.player0 ? 1 : 0, s.player1 ? 1 : 0,
        s.clipKeyCount, s.songAnimKeys, s.diffProxy, s.routineLoaded, s.mergeMoves,
        s.p0SongAnim, s.doSongAnim, s.nativeSetFrameCount,
        s.moveInterpActive ? 1 : 0, s.moveKeyCount, s.songAnimFrameRate,
        s.activeMoveCount,
        s.hudMergeTargetIsHUD ? 1 : 0, s.hudPanelIsHUD ? 1 : 0,
        s.hudHasLeft ? 1 : 0, s.hudHasRight ? 1 : 0, s.hudMDirResolved ? 1 : 0,
        s.footDataValid ? 1 : 0, s.lAnkleZ, s.lToeZ, s.rAnkleZ, s.rToeZ,
        s.lFootZAxisZ, s.rFootZAxisZ, s.lFootInverted ? 1 : 0, s.rFootInverted ? 1 : 0
    );
}
