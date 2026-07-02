#include "telemetry/GameplayTelemetry.h"
#include <cmath>
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
#include "world/CameraShot.h"
#include "rndobj/Cam.h"
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
                // IMPORTANT: Check Dirty() BEFORE WorldXfm()! WorldXfm() triggers
                // WorldXfm_Force() if dirty, which recomputes and clears the flag.
                // We need to capture the dirty state BEFORE that happens.
                s.lAnkleDirty = lAnkle->Dirty();
                s.rAnkleDirty = rAnkle->Dirty();
                {
                    RndTransformable *lShin = lAnkle->TransParent();  // shin
                    RndTransformable *rShin = rAnkle->TransParent();
                    if (lShin) s.lKneeDirty = lShin->Dirty();
                    if (rShin) s.rKneeDirty = rShin->Dirty();
                    RndTransformable *pelvisD = charDir->Find<RndTransformable>("bone_pelvis.mesh", true);
                    if (pelvisD) s.pelvisDirty = pelvisD->Dirty();
#ifdef HX_NATIVE
                    if (pelvisD && getenv("DC3_ROOT_DIAG")) {
                        const Transform &pwx = pelvisD->WorldXfm();
                        RndTransformable *root = pelvisD;
                        int guard = 0;
                        while (root->TransParent() && guard++ < 40) root = root->TransParent();
                        const Transform &rwx = root->WorldXfm();
                        // pelvisUpZ ~ +1 upright; flips toward -1 when the dancer is inverted.
                        fprintf(stderr,
                            "DC3_ROOT_DIAG f=%d pelvisUpY=%.3f pelvisUpZ=%.3f pelvisFwdZ=%.3f "
                            "pelvisPos=(%.1f,%.1f,%.1f) root=%s rootUpY=%.3f rootUpZ=%.3f "
                            "rootPos=(%.1f,%.1f,%.1f)\n",
                            frame, pwx.m.y.y, pwx.m.y.z, pwx.m.z.z,
                            pwx.v.x, pwx.v.y, pwx.v.z,
                            root->Name(), rwx.m.y.y, rwx.m.y.z,
                            rwx.v.x, rwx.v.y, rwx.v.z);
                    }
                    if (getenv("DC3_CAM_DIAG")) {
                        WorldDir *wd = TheHamDirector ? TheHamDirector->GetWorld() : nullptr;
                        CameraManager *cm = wd ? wd->GetCameraManager() : nullptr;
                        CamShot *shot = cm ? cm->CurrentShot() : nullptr;
                        RndCam *wcam = shot ? shot->GetCam() : nullptr;
                        if (wcam) {
                            const Transform &cx = wcam->WorldXfm();
                            // det<0 => improper (mirrored) basis; upZ = m.z.z (world up
                            // component of the cam's up axis) goes negative when flipped.
                            const Vector3 &X = cx.m.x, &Y = cx.m.y, &Z = cx.m.z;
                            float det = X.x * (Y.y * Z.z - Y.z * Z.y)
                                - X.y * (Y.x * Z.z - Y.z * Z.x)
                                + X.z * (Y.x * Z.y - Y.y * Z.x);
                            fprintf(stderr,
                                "DC3_CAM_DIAG f=%d shot=%s shotFrame=%.2f cam=%s upZ=%.3f "
                                "fwdZ=%.3f det=%.3f pos=(%.1f,%.1f,%.1f)\n",
                                frame, shot->Name(), shot->GetFrame(), wcam->Name(), Z.z,
                                Y.z, det, cx.v.x, cx.v.y, cx.v.z);
                        } else if (cm) {
                            fprintf(stderr, "DC3_CAM_DIAG f=%d shot=%s cam=<null>\n", frame,
                                shot ? shot->Name() : "<null>");
                        }
                    }
#endif

                    // Also check thigh (shin's parent) to narrow down cascade source
                    RndTransformable *lThigh = lShin ? lShin->TransParent() : nullptr;
                    RndTransformable *rThigh = rShin ? rShin->TransParent() : nullptr;
                    bool lThighDirty = lThigh && lThigh->Dirty();
                    bool rThighDirty = rThigh && rThigh->Dirty();
                    // Log the full chain once to identify what's dirty
                    static int sDirtyChainLog = 0;
                    if (s.lAnkleDirty && sDirtyChainLog < 5) {
                        sDirtyChainLog++;
                        fprintf(stderr,
                            "DC3_IK_DIAG DirtyChain: ankle=%d shin=%d(%s) thigh=%d(%s) pelvis=%d "
                            "shinChildCount=%d thighChildCount=%d\n",
                            s.lAnkleDirty ? 1 : 0,
                            s.lKneeDirty ? 1 : 0,
                            lShin ? lShin->Name() : "null",
                            lThighDirty ? 1 : 0,
                            lThigh ? lThigh->Name() : "null",
                            s.pelvisDirty ? 1 : 0,
                            lShin ? (int)lShin->Children().size() : -1,
                            lThigh ? (int)lThigh->Children().size() : -1);
                    }
                }
                s.lAnkleZ = lAnkle->WorldXfm().v.z;
                s.lToeZ = lToe->WorldXfm().v.z;
                s.rAnkleZ = rAnkle->WorldXfm().v.z;
                s.rToeZ = rToe->WorldXfm().v.z;
                s.lFootZAxisZ = lAnkle->WorldXfm().m.z.z;
                s.rFootZAxisZ = rAnkle->WorldXfm().m.z.z;

                // PUSH 17 DIAG (DC3_IK_DIAG2): on a FAILING right-foot frame, is the right knee
                // LOCAL bent (plant survived) or ~straight (overwritten)? Walk the render chain.
                if (getenv("DC3_IK_DIAG2") && s.rToeZ < -3.0f) {
                    static int sLegChk = 0;
                    if (sLegChk < 8) { sLegChk++;
                        RndTransformable *rKnee = rAnkle->TransParent();
                        RndTransformable *rThigh = rKnee ? rKnee->TransParent() : nullptr;
                        RndTransformable *rPelvis = rThigh ? rThigh->TransParent() : nullptr;
                        // knee LOCAL rotation x-axis: identity-ish = straight; rotated = bent.
                        const Hmx::Matrix3 &km = rKnee ? rKnee->LocalXfm().m : rAnkle->LocalXfm().m;
                        int kneeDirty = rKnee ? (rKnee->Dirty() ? 1 : 0) : -1;
                        fprintf(stderr,
                            "DC3_IK_DIAG LegChk rToe=%.2f rAnkleZ=%.2f rKneeW=(%.1f) rThighW=(%.1f) rPelvisW=(%.1f) "
                            "kneeLocalRot.x=(%.3f,%.3f,%.3f) rKneeDirty=%d\n",
                            s.rToeZ, s.rAnkleZ,
                            rKnee ? rKnee->WorldXfm().v.z : -999.f,
                            rThigh ? rThigh->WorldXfm().v.z : -999.f,
                            rPelvis ? rPelvis->WorldXfm().v.z : -999.f,
                            km.x.x, km.x.y, km.x.z, kneeDirty);
                    }
                }

                // WAVE 6 LANE A (DC3_KNEE_LOCAL): read bone_L-knee.mesh LOCAL m.x and
                // compute localRotZ = atan2(m.x.y, m.x.x), exactly matching the Xbox
                // Xenia rig (dc3_hack_pack.cc reads the same local m.x). Xbox knee local
                // rotZ ranges -13..-75 (planted beats ~-58); native anim-only ~-20. This
                // tells us whether native's knee LOCAL ever reaches the Xbox bend.
                if (getenv("DC3_KNEE_LOCAL")) {
                    static int sKneeLocal = 0;
                    if (sKneeLocal < 9050) {
                        RndTransformable *lKnee =
                            charDir->Find<RndTransformable>("bone_L-knee.mesh", true);
                        RndTransformable *lThighK =
                            charDir->Find<RndTransformable>("bone_L-thigh.mesh", true);
                        RndTransformable *pelvK =
                            charDir->Find<RndTransformable>("bone_pelvis.mesh", true);
                        if (lKnee) {
                            sKneeLocal++;
                            const Hmx::Matrix3 &km = lKnee->LocalXfm().m;
                            float rotz = 57.29578f * std::atan2(km.x.y, km.x.x);
                            // Ankle local rotZ (foot orientation) — Xbox median +14.7.
                            float ankRotZ = -999.f, ankLocalVx = -999.f;
                            if (lAnkle) {
                                const Hmx::Matrix3 &am = lAnkle->LocalXfm().m;
                                ankRotZ = 57.29578f * std::atan2(am.x.y, am.x.x);
                                ankLocalVx = lAnkle->LocalXfm().v.x;
                            }
                            // Thigh LOCAL matrix m.x — same two elements the Xbox Xenia rig
                            // logs (mxx/mxy), so thigh orientation is directly comparable.
                            // 2026-07-02: segment-drop analysis shows the sink is a thigh
                            // orientation offset (native ~96% vertical vs Xbox ~82-85%),
                            // NOT knee under-bend (mesh knee == blend knee, ratio 1.00).
                            float thMxx = -999.f, thMxy = -999.f, thMxz = -999.f;
                            if (lThighK) {
                                const Hmx::Matrix3 &tm = lThighK->LocalXfm().m;
                                thMxx = tm.x.x; thMxy = tm.x.y; thMxz = tm.x.z;
                            }
                            fprintf(stderr,
                                "DC3_KNEE_LOCAL[%d] kneeRotZ=%.2f kneeVx=%.3f "
                                "ankRotZ=%.2f ankVx=%.3f "
                                "thighMxx=%.5f thighMxy=%.5f thighMxz=%.5f "
                                "pelvisZ=%.2f thighZ=%.2f kneeZ=%.2f ankleZ=%.2f toeZ=%.2f\n",
                                sKneeLocal, rotz, lKnee->LocalXfm().v.x,
                                ankRotZ, ankLocalVx,
                                thMxx, thMxy, thMxz,
                                pelvK ? pelvK->WorldXfm().v.z : -999.f,
                                lThighK ? lThighK->WorldXfm().v.z : -999.f,
                                lKnee->WorldXfm().v.z, s.lAnkleZ, s.lToeZ);
                        }
                    }
                }

                // One-shot diagnostic: dump toe rest-offset and ankle rotation.
                // Gate to gameplay only — we already have rest-pose data.
                static int sFootGeomLog = 0;
                bool isGameplay = (s.state && std::string(s.state) == "playing"
                                   && s.screen && std::string(s.screen) == "game_screen");
                // Also capture a couple of REST-POSE samples (state != playing)
                // so we can compare player0/pelvis Z between rest and gameplay.
                static int sRestGeomLog = 0;
                if (sRestGeomLog < 2 && !isGameplay && lAnkle->WorldXfm().v.z > 3.0f) {
                    sRestGeomLog++;
                    RndTransformable *shinR = lAnkle->TransParent();
                    RndTransformable *thighR = shinR ? shinR->TransParent() : nullptr;
                    RndTransformable *pelvisR = thighR ? thighR->TransParent() : nullptr;
                    RndTransformable *aboveR = pelvisR ? pelvisR->TransParent() : nullptr;
                    fprintf(stderr,
                        "DC3_IK_DIAG RestGeom[%d]: ankleW.z=%.2f toeW.z=%.2f "
                        "pelvisW.z=%.2f abovePW.z=%.2f abovePName=%s "
                        "state=%s screen=%s\n",
                        sRestGeomLog,
                        lAnkle->WorldXfm().v.z,
                        lToe->WorldXfm().v.z,
                        pelvisR ? pelvisR->WorldXfm().v.z : -999.f,
                        aboveR ? aboveR->WorldXfm().v.z : -999.f,
                        aboveR ? aboveR->Name() : "null",
                        s.state ? s.state : "null",
                        s.screen ? s.screen : "null");
                    if (shinR && thighR) {
                        const Transform &kl = shinR->LocalXfm();
                        const Transform &tl = thighR->LocalXfm();
                        fprintf(stderr,
                            "DC3_IK_DIAG RestLocal[%d]: kneeLv=(%.2f,%.2f,%.2f) "
                            "kneeLm.x=(%.3f,%.3f,%.3f) kneeLm.y=(%.3f,%.3f,%.3f) "
                            "kneeLm.z=(%.3f,%.3f,%.3f) | thighLv=(%.2f,%.2f,%.2f) "
                            "thighLm.x=(%.3f,%.3f,%.3f) thighLm.z=(%.3f,%.3f,%.3f)\n",
                            sRestGeomLog,
                            kl.v.x, kl.v.y, kl.v.z,
                            kl.m.x.x, kl.m.x.y, kl.m.x.z,
                            kl.m.y.x, kl.m.y.y, kl.m.y.z,
                            kl.m.z.x, kl.m.z.y, kl.m.z.z,
                            tl.v.x, tl.v.y, tl.v.z,
                            tl.m.x.x, tl.m.x.y, tl.m.x.z,
                            tl.m.z.x, tl.m.z.y, tl.m.z.z);
                    }
                }
                {
                    static int sCharPathLog = 0;
                    if (sCharPathLog < 2 && isGameplay) {
                        sCharPathLog++;
                        fprintf(stderr,
                            "DC3_IK_DIAG CharPath[%d]: ch=%s charDir=%s "
                            "ankleP=%s ankleP_path=%s\n",
                            sCharPathLog,
                            ch ? PathName(ch) : "null",
                            charDir ? PathName(charDir) : "null",
                            lAnkle ? PathName(lAnkle) : "null",
                            lAnkle ? lAnkle->Name() : "null");
                    }
                }
                if (sFootGeomLog < 5 && isGameplay && lAnkle->WorldXfm().v.z < 2.0f) {
                    sFootGeomLog++;
                    const Transform &la = lAnkle->WorldXfm();
                    const Transform &lt = lToe->WorldXfm();
                    const Transform &llo = lToe->LocalXfm();
                    // Walk up the bone chain via TransParent (no Find lookup).
                    RndTransformable *shin = lAnkle->TransParent();
                    RndTransformable *thigh = shin ? shin->TransParent() : nullptr;
                    RndTransformable *pelvisP = thigh ? thigh->TransParent() : nullptr;
                    RndTransformable *aboveP = pelvisP ? pelvisP->TransParent() : nullptr;
                    fprintf(stderr,
                        "DC3_IK_DIAG FootGeom[%d]: ankleW=(%.2f,%.2f,%.2f) "
                        "toeW=(%.2f,%.2f,%.2f) toeLocal=(%.2f,%.2f,%.2f) "
                        "shinWZ=%.2f thighWZ=%.2f pelvisWZ=%.2f abovePWZ=%.2f "
                        "pelvisName=%s aboveName=%s "
                        "ankleM.x=(%.2f,%.2f,%.2f) ankleM.z=(%.2f,%.2f,%.2f)\n",
                        sFootGeomLog,
                        la.v.x, la.v.y, la.v.z,
                        lt.v.x, lt.v.y, lt.v.z,
                        llo.v.x, llo.v.y, llo.v.z,
                        shin ? shin->WorldXfm().v.z : -999.f,
                        thigh ? thigh->WorldXfm().v.z : -999.f,
                        pelvisP ? pelvisP->WorldXfm().v.z : -999.f,
                        aboveP ? aboveP->WorldXfm().v.z : -999.f,
                        pelvisP ? pelvisP->Name() : "null",
                        aboveP ? aboveP->Name() : "null",
                        la.m.x.x, la.m.x.y, la.m.x.z,
                        la.m.z.x, la.m.z.y, la.m.z.z);
                    if (shin && thigh) {
                        const Transform &kl = shin->LocalXfm();
                        const Transform &tl = thigh->LocalXfm();
                        fprintf(stderr,
                            "DC3_IK_DIAG FootLocal[%d]: kneeLv=(%.2f,%.2f,%.2f) "
                            "kneeLm.x=(%.3f,%.3f,%.3f) kneeLm.y=(%.3f,%.3f,%.3f) "
                            "kneeLm.z=(%.3f,%.3f,%.3f) | thighLv=(%.2f,%.2f,%.2f) "
                            "thighLm.x=(%.3f,%.3f,%.3f) thighLm.z=(%.3f,%.3f,%.3f)\n",
                            sFootGeomLog,
                            kl.v.x, kl.v.y, kl.v.z,
                            kl.m.x.x, kl.m.x.y, kl.m.x.z,
                            kl.m.y.x, kl.m.y.y, kl.m.y.z,
                            kl.m.z.x, kl.m.z.y, kl.m.z.z,
                            tl.v.x, tl.v.y, tl.v.z,
                            tl.m.x.x, tl.m.x.y, tl.m.x.z,
                            tl.m.z.x, tl.m.z.y, tl.m.z.z);
                    }
                }
                // Toe above ankle by >2 units = inverted
                s.lFootInverted = (s.lToeZ > s.lAnkleZ + 2.0f);
                s.rFootInverted = (s.rToeZ > s.rAnkleZ + 2.0f);
                // Bone collapse detection
                s.lAnkleX = lAnkle->WorldXfm().v.x;
                s.lAnkleY = lAnkle->WorldXfm().v.y;
                s.rAnkleX = rAnkle->WorldXfm().v.x;
                s.rAnkleY = rAnkle->WorldXfm().v.y;
                float dx = s.lAnkleX - s.rAnkleX;
                float dy = s.lAnkleY - s.rAnkleY;
                float dz = s.lAnkleZ - s.rAnkleZ;
                s.ankleSeparation = std::sqrt(dx*dx + dy*dy + dz*dz);
                // Pelvis-to-ankle distance
                RndTransformable *pelvis = charDir->Find<RndTransformable>("bone_pelvis.mesh", true);
                if (pelvis) {
                    float px = pelvis->WorldXfm().v.x - s.lAnkleX;
                    float py = pelvis->WorldXfm().v.y - s.lAnkleY;
                    float pz = pelvis->WorldXfm().v.z - s.lAnkleZ;
                    s.pelvisToLAnkle = std::sqrt(px*px + py*py + pz*pz);
                }

                // --- "Flying feet" detection ---

                // Helper: check if any component of a Transform has NaN/Inf
                auto xfmHasNaN = [](const Transform &xfm) -> bool {
                    const float *f = &xfm.m.x.x;
                    for (int i = 0; i < 12; i++) {
                        if (!std::isfinite(f[i])) return true;
                    }
                    return false;
                };

                // NaN/Inf in ankle WorldXfm (full 12-component check)
                s.ankleHasNaN = xfmHasNaN(lAnkle->WorldXfm())
                             || xfmHasNaN(rAnkle->WorldXfm());

                // Ankle mLocalXfm values (the IK back-computed local transform)
                s.lAnkleLocalX = lAnkle->LocalXfm().v.x;
                s.lAnkleLocalY = lAnkle->LocalXfm().v.y;
                s.lAnkleLocalZ = lAnkle->LocalXfm().v.z;
                s.rAnkleLocalX = rAnkle->LocalXfm().v.x;
                s.rAnkleLocalY = rAnkle->LocalXfm().v.y;
                s.rAnkleLocalZ = rAnkle->LocalXfm().v.z;

                // NaN/Inf in ankle mLocalXfm
                s.ankleLocalHasNaN = !std::isfinite(s.lAnkleLocalX)
                    || !std::isfinite(s.lAnkleLocalY) || !std::isfinite(s.lAnkleLocalZ)
                    || !std::isfinite(s.rAnkleLocalX)
                    || !std::isfinite(s.rAnkleLocalY) || !std::isfinite(s.rAnkleLocalZ);

                // Frame-to-frame ankle world position delta (detect sudden jumps)
                {
                    static float sPrevLAnkleX = 0, sPrevLAnkleY = 0, sPrevLAnkleZ = 0;
                    static float sPrevRAnkleX = 0, sPrevRAnkleY = 0, sPrevRAnkleZ = 0;
                    static bool sPrevValid = false;
                    if (sPrevValid) {
                        float ldx = s.lAnkleX - sPrevLAnkleX;
                        float ldy = s.lAnkleY - sPrevLAnkleY;
                        float ldz = s.lAnkleZ - sPrevLAnkleZ;
                        s.lAnkleWorldDelta = std::sqrt(ldx*ldx + ldy*ldy + ldz*ldz);
                        float rdx = s.rAnkleX - sPrevRAnkleX;
                        float rdy = s.rAnkleY - sPrevRAnkleY;
                        float rdz = s.rAnkleZ - sPrevRAnkleZ;
                        s.rAnkleWorldDelta = std::sqrt(rdx*rdx + rdy*rdy + rdz*rdz);
                    }
                    sPrevLAnkleX = s.lAnkleX; sPrevLAnkleY = s.lAnkleY; sPrevLAnkleZ = s.lAnkleZ;
                    sPrevRAnkleX = s.rAnkleX; sPrevRAnkleY = s.rAnkleY; sPrevRAnkleZ = s.rAnkleZ;
                    sPrevValid = true;
                }

                // Knee (ankle parent) mLocalXfm — IKElbow modifies parent without
                // back-computing mLocalXfm, which can cause the dirty cascade to
                // produce garbage when the knee WorldXfm is recomputed from stale local.
                RndTransformable *lKnee = lAnkle->TransParent();
                RndTransformable *rKnee = rAnkle->TransParent();
                if (lKnee) s.lKneeLocalX = lKnee->LocalXfm().v.x;
                if (rKnee) s.rKneeLocalX = rKnee->LocalXfm().v.x;
                s.kneeLocalHasNaN = (lKnee && !std::isfinite(lKnee->LocalXfm().v.x))
                                 || (rKnee && !std::isfinite(rKnee->LocalXfm().v.x));

                // Ankle rotation matrix determinant — should be ~1.0 for a proper
                // rotation matrix. Values far from 1.0 indicate a degenerate or
                // corrupted rotation from bad IK back-computation.
                {
                    const Hmx::Matrix3 &m = lAnkle->WorldXfm().m;
                    s.ankleRotDeterminant = m.x.x * (m.y.y * m.z.z - m.y.z * m.z.y)
                                          - m.x.y * (m.y.x * m.z.z - m.y.z * m.z.x)
                                          + m.x.z * (m.y.x * m.z.y - m.y.y * m.z.x);
                }

                // (dirty flag capture moved above, before WorldXfm() calls)
            }

            // Hand bone positions — IKElbow modifies hand parent chain too
            RndTransformable *lHand = charDir->Find<RndTransformable>("bone_L-hand.mesh", true);
            RndTransformable *rHand = charDir->Find<RndTransformable>("bone_R-hand.mesh", true);
            if (lHand) {
                s.lHandX = lHand->WorldXfm().v.x;
                s.lHandY = lHand->WorldXfm().v.y;
                s.lHandZ = lHand->WorldXfm().v.z;
                s.handHasNaN = !std::isfinite(s.lHandX)
                    || !std::isfinite(s.lHandY) || !std::isfinite(s.lHandZ);
            }
            if (rHand) {
                s.rHandX = rHand->WorldXfm().v.x;
                s.rHandY = rHand->WorldXfm().v.y;
                s.rHandZ = rHand->WorldXfm().v.z;
                s.handHasNaN = s.handHasNaN || !std::isfinite(s.rHandX)
                    || !std::isfinite(s.rHandY) || !std::isfinite(s.rHandZ);
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
        "lFootZAxisZ=%.2f rFootZAxisZ=%.2f lFootInverted=%d rFootInverted=%d "
        "ankleSeparation=%.1f pelvisToLAnkle=%.1f "
        "lAnkleX=%.1f lAnkleY=%.1f rAnkleX=%.1f rAnkleY=%.1f "
        "ankleHasNaN=%d handHasNaN=%d "
        "lAnkleLocalX=%.2f lAnkleLocalY=%.2f lAnkleLocalZ=%.2f "
        "rAnkleLocalX=%.2f rAnkleLocalY=%.2f rAnkleLocalZ=%.2f "
        "ankleLocalHasNaN=%d "
        "lAnkleWorldDelta=%.2f rAnkleWorldDelta=%.2f "
        "lHandX=%.1f lHandY=%.1f lHandZ=%.1f rHandX=%.1f rHandY=%.1f rHandZ=%.1f "
        "lKneeLocalX=%.2f rKneeLocalX=%.2f kneeLocalHasNaN=%d "
        "ankleRotDeterminant=%.4f "
        "lAnkleDirty=%d rAnkleDirty=%d lKneeDirty=%d rKneeDirty=%d pelvisDirty=%d\n",
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
        s.lFootZAxisZ, s.rFootZAxisZ, s.lFootInverted ? 1 : 0, s.rFootInverted ? 1 : 0,
        s.ankleSeparation, s.pelvisToLAnkle,
        s.lAnkleX, s.lAnkleY, s.rAnkleX, s.rAnkleY,
        s.ankleHasNaN ? 1 : 0, s.handHasNaN ? 1 : 0,
        s.lAnkleLocalX, s.lAnkleLocalY, s.lAnkleLocalZ,
        s.rAnkleLocalX, s.rAnkleLocalY, s.rAnkleLocalZ,
        s.ankleLocalHasNaN ? 1 : 0,
        s.lAnkleWorldDelta, s.rAnkleWorldDelta,
        s.lHandX, s.lHandY, s.lHandZ, s.rHandX, s.rHandY, s.rHandZ,
        s.lKneeLocalX, s.rKneeLocalX, s.kneeLocalHasNaN ? 1 : 0,
        s.ankleRotDeterminant,
        s.lAnkleDirty ? 1 : 0, s.rAnkleDirty ? 1 : 0,
        s.lKneeDirty ? 1 : 0, s.rKneeDirty ? 1 : 0,
        s.pelvisDirty ? 1 : 0
    );
}
