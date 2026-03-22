# Boot-to-Gameplay Flow: Xbox vs Native

Complete trace of every screen transition, DTA handler, panel load, and message
dispatch from application boot to a song playing with characters dancing.

---

## 1. Application Entry (`App::App()`)

### Xbox Boot Sequence

```
App::App()
  EnableKeyCheats(false)
  SetFileChecksumData()
  SystemPreInit("config/ham_preinit_keep.dta")
  TheArchive->SetArchivePermission(...)
  TheRnd.PreInit()
  set notify_level
  TheDebug.SetModalCallback(DebugModal)
  SynthPreInit()
  Movie::Init()
  Splash splash (ESRB + Harmonix splash screens)
    splash.AddScreen("ui/splash/eng/esrb_keep.milo", 0x12C0)
    splash.AddScreen("ui/splash/harmonix_keep.milo", 3000)
    splash.PrepareNext() / BeginSplasher()
  LiveCameraInput::PreInit() / Init()    // Kinect camera
  KinectGuideThread (background thread)  // NUI skeleton tracking
  splash.PrepareRemaining()
  SystemInit("config/ham_keep.dta")      // loads ham_keep.dta, runs DTA init
  MagnuInit()
  TheRnd.Init()
  TheServer.Init()
  TheRockCentral.Init()
  FixedSizeSaveable::Init()
  HamUserMgrInit(false)
  SynthInit()
  FlowInit()
  sfx/audio_mixer.milo (loaded)
  sound bank "common" (loaded)
  SaveLoadManager::Init()
  CharInit()
  MidiParser::Init()
  WorldInit()
  HamInit()
  TheHamSongMgr.Init()
  MetaPanel::Init()
  GameInit()                             // registers GamePanel, GameMode factories
  DirLoader::SetPathEvalCallback(IsUselessLoad)
  ContextCheckerInit()
  AccomplishmentManager::Init()
  MetagameRank::Init()
  persistent FileCache (loaded)
  TheUI->Init()                          // calls UIManager::Init() which processes ui.dta
  GestureMgr::DebugInit()
  ThePresenceMgr.Init()
  MoveMgr::Init(0)
  MiniGameMgr::Init()
  PartyModeMgr::Init()
  TheUI->GotoFirstScreen()               // $first_screen = attract_screen
  splash.EndSplasher()
  EnableKeyCheats(true)
  KinectGuideThread join + close
```

### Native Boot Sequence

```
App::App()
  EnableKeyCheats(false)
  SetFileChecksumData()
  SystemPreInit("config/ham_preinit_keep.dta")
  TheRnd.PreInit()
  set notify_level = 1
  TheDebug.SetModalCallback(DebugModal)
  [Emscripten only: DirLoader::SetCacheMode(true)]
  SystemInit("config/ham_keep.dta")
  SynthInit()                            // audio subsystem
  Movie::Init()
  TheRnd.Init()
  MagnuInit()
  FlowInit()
  sound bank "common" (loaded)
  CharInit()
  WorldInit()
  HamInit()
  REGISTER_OBJ_FACTORY(AppLabel)         // HamLabel -> AppLabel override
  Player provider wiring (player_provider_0/1 creation)
  MoveMgr::Init(0)
  MiniGameMgr::Init()
  TheHamSongMgr.Init()
  MetaPanel::Init()
  GameInit()
  MidiParser::Init()
  DirLoader::SetPathEvalCallback(IsUselessLoad)
  ContextCheckerInit()
  TheContentMgr.RefreshSynchronously()   // scan ark for songs
  TheUI = &TheHamUI
  TheHamUI.Init()                        // UIEventMgr + UIManager + ShellInput
  Register smart stubs:
    saveload_mgr  -> NativeSaveLoadStub   (always idle, initial load done)
    profile_mgr   -> NativeProfileMgrStub (no profiles, tutorials seen, unlocked)
    platform_mgr  -> NativePlatformMgrStub (no guide, no Live)
    content_mgr   -> Hmx::Object stub
    challenges    -> Hmx::Object stub
    speech_mgr    -> NativeSpeechMgrStub  (no recognition)
  TheUI->GotoFirstScreen()               // $first_screen = attract_screen
```

### Gap: Boot Differences

| Aspect | Xbox | Native | Gap |
|--------|------|--------|-----|
| Splash screens | ESRB + Harmonix via Splash class | None | Cosmetic only |
| Kinect | LiveCameraInput + KinectGuideThread | None | Not needed |
| SaveLoadManager | Real save system, Init() creates singleton | NativeSaveLoadStub always returns idle | Save data not persisted |
| Profile system | Real HamUserMgr, profiles from hard drive | NativeProfileMgrStub, no real profiles | Cannot track unlocks/progress |
| XBL networking | RockCentral, Achievements, Leaderboards | Not initialized | Not needed for gameplay |
| Content refresh | Async via ContentMgr | Synchronous via RefreshSynchronously() | OK -- works |
| Player providers | Created by ham_init.dta | Explicitly created in C++ | OK -- works |
| Sound bank | audio_mixer.milo + common bank | common bank only | audio_mixer missing |

---

## 2. Screen Flow: Boot to Main Menu

### Xbox Flow (DTA-driven)

```
$first_screen = attract_screen       (set in ui/ui.dta line 17)

attract_screen
  panels: attract_movie_panel, movie_overlay_panel
  Plays attract video (BINK)
  movie_done handler -> goto next_screen (= autosave_warning_screen)
  -OR- user presses button -> skip_selected -> goto autosave_warning_screen

autosave_warning_screen
  panels: autosave_warning_panel
  enter handler:
    - {hamprovider set ui_nav_mode title}
    - {title_screen set check_for_nag TRUE}
    - starts a 4-second timer
  poll handler:
    - After 4 seconds: goto title_screen
  unload handler:
    - background_panel load TRUE
    - correct_identity_panel load TRUE
    - pause_panel, dialog_panel, loading panels... (bulk panel preload)

title_screen
  panels: background_panel, title_panel, tutorial_nav_panel, meta
  title_panel enter:
    - {hamprovider set ui_nav_mode title}
    - {speech_mgr begin_recognition TRUE}
    - {voice_input_panel activate_voice_context title_screen}
  NAV_SELECT_MSG:
    title_screen_menu:
      {skeleton_identifier set_up_initial_profiles}
      {set $post_load_dest_screen main_screen}
      {ui goto_screen wait_main_after_saveload_screen}

wait_main_after_saveload_screen
  panels: meta, background_panel, empty_postproc_panel,
          wait_for_saveload_panel, title_panel, tutorial_nav_panel
  enter: {saveload_mgr activate}       // start save/load process
  wait_for_saveload_panel poll:
    - Waits until {saveload_mgr is_idle} is TRUE
    - Then calls {current_screen saveload_complete}
  saveload_complete handler:
    - {content_mgr start_refresh}
    - Voice control tutorial check
    - Profile nag check
    - {ui goto_screen $post_load_dest_screen}  // -> main_screen

main_screen
  panels: meta, background_panel, main_panel,
          main_menu_wait_for_content_panel
  main_panel enter:
    - {platform_mgr add_sink $this (connection_status_changed)}
    - {hamprovider set ui_nav_mode shell}
    - {gamemode set_mode init}
    - {meta music_start}
    - {content_mgr start_refresh}
    - Updates provider list
```

### Native Flow (auto-advance in UI::Poll)

```
$first_screen = attract_screen       (same DTA)
MILO_FIRST_SCREEN env can override

Screen advance table in UI::Poll() (src/system/ui/UI.cpp:598-613):
  attract_screen        -> title_screen                 (delay 1 frame)
  autosave_warning_screen -> title_screen               (delay 90 frames ~3s)
  title_screen          -> wait_main_after_saveload_screen (delay 60 frames ~2s)
  wait_main_after_saveload_screen -> main_screen        (delay 120 frames ~4s)
  tutorial screens      -> main_screen                  (delay 1 frame)

Boot flow on native:
  attract_screen (1 frame) -> title_screen
  DTA enter handlers fire but many fail silently:
    - {speech_mgr begin_recognition TRUE} -> stub returns 0
    - {voice_input_panel activate_voice_context ...} -> silently fails
  title_screen (60 frames) -> wait_main_after_saveload_screen
  DTA enter: {saveload_mgr activate} -> NativeSaveLoadStub returns 0
  wait_for_saveload_panel poll:
    {saveload_mgr is_idle} -> NativeSaveLoadStub returns 1 (always idle)
    -> saveload_complete fires
    -> $post_load_dest_screen not set -> UNCLEAR behavior
  UI::Poll auto-advance table catches it -> main_screen (120 frames)

TOTAL TIME TO MAIN_SCREEN: ~7 seconds of frame-counting delays
```

### Gap: Boot-to-Main

| Aspect | Xbox | Native | Gap |
|--------|------|--------|-----|
| Attract video | Plays BINK video | Skipped (no video decoder) | Cosmetic |
| Autosave warning | 4s timer in DTA | Auto-advanced by C++ | OK |
| Save/load | Real save system waits for completion | Stub is always idle, fires immediately | OK |
| Profile setup | skeleton_identifier set_up_initial_profiles | Skipped (no Kinect) | Missing but not needed |
| Voice tutorials | Checked and shown if unseen | NativeProfileMgrStub says "already seen" | OK |
| $post_load_dest_screen | Set to main_screen by title_panel NAV_SELECT | May not be set on auto-advance | Potential bug -- fallback to auto-advance table covers it |
| Content refresh | Async, wait_for_content_panel gates | Already done synchronously at boot | OK |

---

## 3. Main Menu to Song Select

### Xbox Flow

```
main_screen -> user selects "gameplay"
  main_panel NAV_SELECT_MSG:
    case "gameplay":
      {gamemode set_mode init}
      {ui goto_screen choose_mode_screen}

choose_mode_screen
  panels: meta, background_panel, choose_mode_panel
  enter:
    - {gamemode set_mode init}
    - {hamprovider set is_in_campaign_mode FALSE}
  NAV_SELECT_MSG:
    case "perform":
      {gamemode set_mode perform}
      {song_select_panel set first_time TRUE}
      {ui goto_screen {gamemode get newsong_screen}}

  newsong_screen (from modes.dta):
    For "perform" mode: song_select_screen
    For campaign: campaign_songselect_screen

song_select_screen
  panels: song_select_panel + supporting panels
  User browses songs, selects one
  -> {meta_performer set_song <song>}
  -> {ui goto_screen {gamemode get ready_screen}}

  ready_screen = multiuser_screen (from modes.dta)
```

### Native Flow (DC3_SCREEN=game_screen auto-nav)

```
When DC3_SCREEN env is set, App::RunWithoutDebugging() auto-navigates:

At main_screen:
  sGameSetupDone = false -> sets up game:
    - TheGameData->SetSong(DC3_SONG or "boyfriend")
    - TheGameData->SetVenue(DC3_VENUE or song metadata venue or "glitterati")
    - TheGameMode->SetMode("perform", "none")
    - TheHamProvider->SetProperty("merge_moves", 1)
    - TheHamProvider->SetProperty("use_movegraph", 1)
    - Player difficulty from DC3_DIFFICULTY (default: kDifficultyEasy)
    - Player autoplay from DC3_AUTOPLAY (default: "maximum")
  -> GotoScreen("choose_mode_screen")

At choose_mode_screen:
  -> GotoScreen("song_select_screen")

At song_select_screen:
  -> GotoScreen("multiuser_screen")

At multiuser_screen:
  -> GotoScreen("loading_screen")
```

### Gap: Menu to Song Select

| Aspect | Xbox | Native | Gap |
|--------|------|--------|-----|
| Mode selection | User selects mode (perform/battle/practice) | Hardcoded "perform" | Only perform mode tested |
| Song selection | Song_select_panel with full browse | Hardcoded from DC3_SONG env | No interactive song select |
| MetaPerformer | set_song via DTA handler | TheGameData->SetSong() directly | MetaPerformer::SetSong() may not fire -- CRITICAL: MetaPerformer creates the FileMerger load_song dispatch |
| Multiuser screen | Character/outfit/difficulty selection | Skipped entirely | Character/outfit not selected by user |
| enter_gameplay() | Called from multiuser/startgame DTA | Never called on native | CRITICAL GAP -- see Section 4 |

---

## 4. Song Select to Loading Screen

### Xbox Flow (enter_gameplay)

```
User confirms on multiuser_screen or startgame_panel:
  -> {enter_gameplay}   (defined in global.dta:212)

enter_gameplay:
  1. {reset_loading_music_mogg}    // pick era-appropriate loading music
  2. Tutorial checks (skip if kTutorialGeneral seen)
  3. {initialize_gameplay_data}
       - {hamprovider set finale FALSE}
       - {hamprovider set golden_boomy ...}
  4. {gesture_mgr set_identification_enabled FALSE}
  5. {ui force_letterbox_off_immediate}
  6. {ui goto_screen loading_screen} -OR- {ui pop_screen loading_screen}
  7. {meta music_stop}

loading_screen (loading.dta)
  panels: loading_panel, rhythm_detector_panel
  enter:
    - {hamprovider set ui_nav_mode loading}
    - {ui force_letterbox_off_immediate}
    - {ui goto_screen preloading_screen}

preloading_screen
  panels: loading_panel, rhythm_detector_panel, preload_panel
  preload_panel:
    - Caches song .milo, .mid, barks files
    - on_preload_ok -> {ui goto_screen real_loading_screen}

real_loading_screen
  panels: loading_panel, rhythm_detector_panel
  enter:
    - {song_mgr add_recent_song {gamedata get song}}
    - {synth stop_all_sfx}
    - {ui goto_screen {gamemode get game_screen}}
      // game_screen from modes.dta for "perform" mode = game_screen
```

### Loading Panel Internals

```
loading_panel (LoadingPanel class):
  Load():
    - Creates sLoadingMaster (HamMaster for loading music)
    - PlayLoadingMusic() using $loading_music_mogg
  Enter():
    - SetSecondsAndBeat(0, 0, true)
    - Starts loading music stream
  IsLoaded():
    - TheContentMgr.RefreshDone()
    - UIPanel::IsLoaded()
    - loading music ready (or failed)
  Unload():
    - delete sLoadingMaster
```

### Native Flow

```
DC3_SCREEN auto-nav sends us directly:
  multiuser_screen -> loading_screen

NO enter_gameplay() call!
  Missing: initialize_gameplay_data, reset_loading_music_mogg, meta music_stop

Loading screen DTA enter fires:
  {ui goto_screen preloading_screen}

  preloading_screen preload_panel:
    song_mgr song_file_path -> may fail if MetaPerformer not set
    on_preload_ok -> real_loading_screen

  real_loading_screen enter:
    {ui goto_screen game_screen}
```

### Gap: Loading Transition

| Aspect | Xbox | Native | Gap |
|--------|------|--------|-----|
| enter_gameplay() | Called, sets up game state | Never called | CRITICAL: missing initialize_gameplay_data, loading music selection |
| Loading music | Era-specific mogg selected, played during load | $loading_music_mogg empty string | Cosmetic |
| Preload panel | Caches song .milo/.mid from ark | Works if song exists on disc | OK for on-disc songs |
| Song routing | {gamedata get song} set by MetaPerformer flow | Set directly by C++ auto-nav | Works but may miss MetaPerformer state |

---

## 5. Loading Screen to Gameplay (game_screen)

### Xbox: game_screen Enter

```
game_screen (game.dta)
  panels: game_panel, world_panel, rhythm_detector_panel,
          bustamove_visualizer_panel, bustamove_panel,
          flashcard_dock_panel, fitness_hud_panel

  world_panel:
    file: "../world/world.milo"   // loads the world root (contains world.fm)

  enter handler:
    - {$this set_showing TRUE}
    - {rnd set_in_game TRUE}
    - {platform_mgr add_sink ...}
    - {$this reset_check_states}
    - {$this reset_game_mode}
    - {init_game_snapshots}
    - {flashcard_dock_panel set_showing FALSE}
    - {ui force_letterbox_off}

  reset_game_mode handler:
    - {game_panel set_type {gamemode get gameplay_mode}}
    - {handle (game_panel init)}
```

### Xbox: GamePanel Load/Enter Pipeline

```
GamePanel::Load():
  1. mPerformanceProfiler.Start()
  2. CreateGame() -> new Game()
  3. UIPanel::Load()

Game::Game() constructor:
  1. new SongDB()
  2. new MidiParserMgr()
  3. new HamMaster(songData, midiParserMgr)
  4. SetBackgroundVolume / SetForegroundVolume
  5. LoadSong()

Game::LoadSong():
  1. TheGameData->GetSong() -> song symbol
  2. MetaPerformer::Current()->Handle(Message("on_load_song"))
  3. Check use_movegraph from GameMode
  4. TheMoveMgr->Clear() / SetSong()
  5. new SongInfoCopy(SongAudioData(song))
  6. mMaster->Load(songInfo, ...)

GamePanel::PollForLoading():  (called each frame during transition)
  State 0 -> UIPanel::IsLoaded() check
  State 1 -> world_panel + HamDirector::IsWorldLoaded() check
  State 2 -> HamWardrobe::AllCharsLoaded() check (if load_chars)
  State 3 -> Game::IsReady() -> Game::IsLoaded()
  State 4 -> DONE -- gameplay can begin

Game::IsLoaded():
  mLoadState 0:
    - mMaster->IsLoaded() (song data parsed)
    - HamDirector::IsWorldLoaded() (if movegraph)
    - TheSongDB->PostLoad()
    - Game::PostLoad() -> finds MoveDir, creates Overshell
    - TheMoveMgr->LoadMoveData()
    -> mLoadState = 1
  mLoadState 1:
    - HamDirector::IsMoveMergerFinished()
    -> mLoadState = 2
  mLoadState 2:
    - mMaster->GetAudio()->IsReady() (audio stream buffered)
    -> mLoadState = 3
    -> TheProfileMgr.PushAllOptions()

GamePanel::Enter():
  1. ClearTimelineTasks
  2. UIPanel::Enter()
  3. Reset()
  4. SetPaused(false)
  5. ThePresenceMgr.SetInGame()
```

### Xbox: HamDirector Song Loading Pipeline

```
world.milo loads -> contains world.fm (FileMerger)

world.fm (char_objects.dta defines behavior):
  change_files callback:
    - {$hamdirector set merger $this}
    - {$hamdirector load_game_song FALSE}

  HamDirector::OnLoadSong(DataArray*):
    1. Read player crews/outfits from TheGameData
    2. Determine song speed (slow/medium/fast by BPM)
    3. mMerger->Select("song", songPath, true)
    4. mMerger->StartLoad(async)

  on_pre_merge -> OnFileLoaded:
    When sym == "song":
      - TheHamWardrobe->LoadCharacters(outfits, crews, backupDancers, speed, venue)
      - mMerger->Select("viz", "ui/visualizer/visualizer.milo")
      - GetVenuePath(path, venue) -> mMerger->Select("venue", path)
      - mGameModeMerger->HandleType("load_game_hud") -> select HUD milo
      - mGameModeMerger->StartLoad()
      - mMerger->StartLoad()  // loads venue + visualizer

    When sym == "venue":
      - mVenue = dynamic_cast<WorldDir*>(dir)

    When sym == "viz":
      - mVisualizer = dynamic_cast<HamVisDir*>(dir)

  HamDirector::Enter():
    - mWorldPostProc setup
    - VenueEnter(mVenue)  -- finds player0/1/backup0/1 characters
    - Initialize()
    - SongAnim(0)->StartAnim()
    - SyncScene()
    - PlayIntroShot()
    - TheHamWardrobe->PlayCrowdAnimation("realtime_idle")

  GameModeMerger (char_objects.dta:466):
    load_game_hud handler selects HUD milo by gameplay_mode:
      perform -> "ui/hud/_perform_hud.milo"
      dance_battle -> "ui/hud/_dance_battle_hud.milo"
      holla_back -> "ui/hud/_holla_back_hud.milo"
      etc.
```

### Xbox: Gameplay Loop

```
Game::Poll() (called from GamePanel::Poll()):
  1. HandleWait() -- manages intro/start/restart/jump states
  2. mGameInput->CurrentMs() -> TheTaskMgr.SetSeconds()
  3. mMaster->Poll(songMs) -> MidiParser, audio sync
  4. CalcSongPos -> TheTaskMgr.SetSongPos()

HamDirector::Poll():
  1. TheHamWardrobe->UpdateOverlay()
  2. SongAnim(0) -> PlayAnims for player0, player1, backups
  3. mPoseFatalities->Poll()
  4. mVenue->Poll()  // WorldDir poll -- lighting, cameras
  5. PostProc interpolation
  6. Visualizer freestyle check

GamePanel::Poll():
  1. SetSoundEventReceiver()
  2. Pause count-in check
  3. UIPanel::Poll()
  4. State machine: 0 -> StartIntro, kGameInIntro -> StartGame
  5. Game::Poll()
  6. FitnessFilter polls
  7. MoveDir filter updates
  8. Debug overlays (time, latency, fitness, loop viz)
```

### Native: game_screen Enter

```
Same DTA enter handler fires:
  {rnd set_in_game TRUE}
  {$this reset_game_mode}
  etc.

world_panel loads "../world/world.milo"
  This contains world.fm -> triggers the full FileMerger pipeline

GamePanel::Load() -> CreateGame() -> Game() constructor
  Game::LoadSong() -> TheGameData->GetSong() -> set during auto-nav

GamePanel::PollForLoading():
  Same state machine, but with native fallbacks:
  - Audio timeout (120 polls) if audio never becomes ready
  - mMoveDir null tolerance
  - Force-advance past kGameInIntro after 30 frames

App::RunWithoutDebugging() main loop:
  - SystemPoll
  - TheUI->Poll()
  - TheTaskMgr.Poll()
  - TheFlowMgr->Poll()
  - TheSynth->Poll()
  - Venue setup (one-shot):
      - Hide Kinect meshes (TVScreen, projection, Reflect, refract)
      - Hide TexRenderers
  - Venue poll (menu venues only -- gameplay venues polled by HamDirector)
  - BeginDrawing -> TheUI->Draw() -> EndDrawing

HamUI::Draw() (native):
  Two-pass draw:
  1. UIManager::Draw() (all screen panels including game_screen/world_panel)
  2. HelpBar, Letterbox, Blacklight, Overlay
```

### Gap: Loading to Gameplay

| Aspect | Xbox | Native | Gap |
|--------|------|--------|-----|
| world.fm pipeline | Full: song -> venue -> viz -> HUD all via FileMerger | Same pipeline fires via DTA | OK -- this is the core convergence win |
| HamDirector::OnLoadSong | Called from world.fm change_files callback | Same callback fires | OK |
| Character loading | TheHamWardrobe->LoadCharacters() from OnFileLoaded("song") | Same code fires, with native crew/outfit reconstruction | OK (native has fallback logic for null crews) |
| Venue loading | OnFileLoaded("venue") sets mVenue | Same | OK |
| HUD loading | GameModeMerger load_game_hud selects per-mode HUD | Same DTA handler fires | OK |
| HamDirector::Enter() | VenueEnter, Initialize, SyncScene, PlayIntroShot | Same code | OK |
| Game intro | StartIntro -> kGameInIntro -> wait for intro timer | Force-advance after 30 frames | Skips intro sequence |
| Audio sync | LiveInput from Kinect timing | mGameInput may be null, timeouts | Audio may not play |
| Kinect gesture | GestureMgr provides skeleton data | No skeleton data | Autoplay compensates |
| Song sequence | SongSequence manages multi-song playlists | Not exercised | Single-song OK |

---

## 6. During Gameplay: Render + Animation Pipeline

### Xbox: Full Pipeline

```
FRAME:
  1. GestureMgr::Poll()     -- Kinect skeleton tracking
  2. HamUI::Poll()           -- Game pause check, shell input
  3. UIManager::Poll()       -- Screen transitions, panel polls
     -> GamePanel::Poll()
        -> Game::Poll()
           -> HandleWait()
           -> mGameInput->CurrentMs()
           -> mMaster->Poll(songMs)  -- MIDI events, audio sync
        -> HamDirector::Poll()
           -> SongAnim: play clip anims for player0/1/backups
           -> mPoseFatalities->Poll()
           -> mVenue->Poll()         -- WorldDir: cameras, lights
           -> PostProc interpolation
  4. SkeletonUpdate::PostUpdate()    -- game->PostUpdate (Overshell)
  5. DrawRegular()
     -> TheUI->Draw() = HamUI::Draw()
        -> UIManager::Draw()         -- draws game_screen panels
           -> world_panel->DrawShowing()
              -> HamDirector::DrawShowing()
                 -> mVenue->DrawShowing()  -- full venue render
           -> game_panel (typeDef handlers for HUD)
        -> HelpBar, Letterbox, Blacklight
        -> Augmented photo
```

### Native: Pipeline

```
FRAME (App::RunWithoutDebugging):
  1. SystemPoll(false)
  2. TheUI->Poll() = HamUI::Poll()
     -> GamePanel::Poll()
        -> Game::Poll()       (same logic, with null guards)
        -> No SkeletonUpdate  (no Kinect)
  3. TheTaskMgr.Poll()
  4. TheFlowMgr->Poll()
  5. TheSynth->Poll()
  6. Venue one-shot setup (hide Kinect meshes/TexRenderers)
  7. BeginDrawing
     -> Pre-game venue draw (if no HamDirector)
     -> TheUI->Draw()        (same HamUI::Draw pipeline)
        -> game_screen panels render via UIManager::Draw
     -> EndDrawing
```

### Gap: Gameplay Runtime

| Aspect | Xbox | Native | Gap |
|--------|------|--------|-----|
| Skeleton input | Kinect via GestureMgr | None | Autoplay must compensate |
| Autoplay | Debug tool for testing | Primary input method | OK, works well |
| Audio playback | Xbox audio system (XMA) | Vorbis via AudioDevice | Works for .mogg files |
| Audio sync | LiveInput precise timing | LiveInput with fallbacks | May drift |
| Move detection | Real body tracking | Autoplay simulated | OK |
| Camera system | CameraManager + CamShots from song.anim | Same system via HamDirector | OK |
| Lighting | PropAnims drive RndLights | Same system | OK |
| PostProc | Xbox-specific GPU effects | WGPU PostProc (partially implemented) | Visual differences |
| HUD | Loaded by GameModeMerger | Same pipeline | OK |
| Crowd | TheHamWardrobe::PlayCrowdAnimation | Same if wardrobe loads | OK |

---

## 7. Critical Gaps Summary

### Must Fix for Convergence

1. **enter_gameplay() never called on native auto-nav path**.
   The function `enter_gameplay` (global.dta:212) does critical setup:
   - `initialize_gameplay_data` (sets finale/golden_boomy provider state)
   - `gesture_mgr set_identification_enabled FALSE`
   - `ui force_letterbox_off_immediate`
   - `meta music_stop`

   FIX: Either call the DTA function from C++, or replicate its effects in
   the auto-nav C++ code in App::RunWithoutDebugging().

2. **MetaPerformer not set up**.
   Xbox flow calls `{meta_performer set_song <song>}` from song_select DTA.
   Native auto-nav sets `TheGameData->SetSong()` directly but never calls
   `MetaPerformer::SetSong()`. This may affect:
   - Song sequence (playlist support)
   - End-game results
   - Loading screen music selection

   FIX: Call `MetaPerformer::Current()->SetSong(songSym)` in the auto-nav
   setup code, or execute the DTA function.

3. **GameMode enter handler not fired**.
   `modes.dta` defines COMMON_ENTER_HANDLER which sets many hamprovider
   properties (requires_2_players, use_movegraph, merge_moves, etc.).
   Native auto-nav sets some of these in C++ but may miss others.

   FIX: After `TheGameMode->SetMode()`, call the mode's enter handler
   or ensure all required properties are set.

### Nice to Have

4. **$post_load_dest_screen** may not be set when the auto-advance table
   fires the saveload_complete handler. This works because the C++ table
   catches it, but it means some DTA state is undefined.

5. **Loading music** ($loading_music_mogg) is never set because
   `reset_loading_music_mogg` requires `hamprovider get current_campaign_era`
   which may fail. Cosmetic only.

6. **Game intro sequence skipped**. Native force-advances past kGameInIntro
   after 30 frames. The intro countdown (negative seconds) + intro camera
   shot never play. Cosmetic.

---

## 8. Complete Screen Chain Reference

### Xbox: Boot to Gameplay
```
attract_screen
  -> autosave_warning_screen          (movie_done or skip)
    -> title_screen                   (4s timer)
      -> wait_main_after_saveload_screen  (NAV_SELECT: title_screen_menu)
        -> main_screen                (saveload_complete)
          -> choose_mode_screen       (NAV_SELECT: gameplay)
            -> song_select_screen     (NAV_SELECT: perform)
              -> multiuser_screen     ({gamemode get ready_screen})
                -> loading_screen     ({enter_gameplay})
                  -> preloading_screen (enter)
                    -> real_loading_screen  (on_preload_ok)
                      -> game_screen   ({gamemode get game_screen})
```

### Native: Boot to Gameplay (DC3_SCREEN=game_screen)
```
attract_screen
  -> title_screen                     (auto-advance, 1 frame)
    -> wait_main_after_saveload_screen  (auto-advance, 60 frames)
      -> main_screen                  (auto-advance, 120 frames)
        -> choose_mode_screen         (DC3_SCREEN auto-nav)
          -> song_select_screen       (auto-nav)
            -> multiuser_screen       (auto-nav)
              -> loading_screen       (auto-nav)
                -> preloading_screen  (DTA enter)
                  -> real_loading_screen  (DTA on_preload_ok)
                    -> game_screen    (DTA enter)
```

---

## 9. Key Source Files

| File | Role |
|------|------|
| `src/App.cpp` | Boot sequence, main loop, auto-nav logic |
| `src/system/ui/UI.cpp` | GotoFirstScreen, auto-advance table |
| `orig-assets/extracted/ui/ui.dta` | $first_screen = attract_screen, DTA init chain |
| `orig-assets/extracted/ui/init.dta` | DTA include order for all screen definitions |
| `orig-assets/extracted/ui/global.dta` | enter_gameplay(), loading music, helper functions |
| `orig-assets/extracted/ui/title/title.dta` | title_screen, wait_main_after_saveload_screen |
| `orig-assets/extracted/ui/title/autosave_warning.dta` | autosave_warning_screen, panel preloads |
| `orig-assets/extracted/ui/main/main.dta` | main_screen, main_panel NAV_SELECT handlers |
| `orig-assets/extracted/ui/choose_mode/choose_mode.dta` | Mode selection -> newsong_screen routing |
| `orig-assets/extracted/ui/loading/loading.dta` | loading_screen -> preloading -> real_loading chain |
| `orig-assets/extracted/ui/game.dta` | game_screen definition, world_panel, GamePanel |
| `orig-assets/extracted/config/modes.dta` | GameMode properties: game_screen, newsong_screen, etc. |
| `orig-assets/extracted/char/char_objects.dta` | world.fm / game_mode.fm callbacks, load_game_hud |
| `src/lazer/game/Game.cpp` | Game constructor, LoadSong, IsLoaded state machine |
| `src/lazer/game/GamePanel.cpp` | PollForLoading, StartGame, CreateGame |
| `src/system/hamobj/HamDirector.cpp` | OnLoadSong, OnFileLoaded, Enter, Poll, VenueEnter |
| `src/lazer/meta_ham/HamUI.cpp` | HamUI::Init, Poll, Draw (two-pass) |
| `src/lazer/meta_ham/LoadingPanel.cpp` | Loading music, IsLoaded gate |

---

## 10. Implementation Checklist

To bring the native port to full Xbox-parity gameplay flow:

- [ ] Call `enter_gameplay` equivalent before navigating to loading_screen
  - `initialize_gameplay_data` (set hamprovider finale/golden_boomy)
  - `gesture_mgr set_identification_enabled FALSE`
  - `ui force_letterbox_off_immediate`
  - `meta music_stop`
- [ ] Set up MetaPerformer with selected song before loading
- [ ] Fire GameMode enter handler after SetMode() to populate hamprovider
- [ ] Verify $post_load_dest_screen is set for DTA flows that read it
- [ ] Remove hard-coded auto-advance delays in UI::Poll (use DTA flow instead)
- [ ] Allow interactive title_screen -> main_screen flow (for windowed mode)
- [ ] Test with different game modes (dance_battle, practice, etc.)
- [ ] Verify loading music plays (set $loading_music_mogg from era)
- [ ] Test song sequence / playlist support via MetaPerformer
- [ ] Verify end-game flow (game_won -> perform_endgame_screen)
