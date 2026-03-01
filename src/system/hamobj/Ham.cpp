#include "hamobj/Ham.h"
#include "CharFeedback.h"
#include "HamCharacter.h"
#include "HamDriver.h"
#include "HamMove.h"
#include "HamRegulate.h"
#include "HamSong.h"
#include "MoveGraph.h"
#include "PoseFatalities.h"
#include "SongCollision.h"
#include "flow/PropertyEventProvider.h"
#include "gesture/Gesture.h"
#include "gesture/SpeechMgr.h"
#include "hamobj/BustAMoveData.h"
#include "hamobj/CamShotCatVO.h"
#include "hamobj/CrazeHollaback.h"
#include "hamobj/DanceRemixer.h"
#include "hamobj/DancerSequence.h"
#include "hamobj/Difficulty.h"
#include "hamobj/HamBattleData.h"
#include "hamobj/HamCamShot.h"
#include "hamobj/HamCamTransform.h"
#include "hamobj/HamDirector.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamIKEffector.h"
#include "hamobj/HamIKSkeleton.h"
#include "hamobj/HamIconMan.h"
#include "hamobj/HamLabel.h"
#include "hamobj/HamList.h"
#include "hamobj/HamListRibbon.h"
#include "hamobj/HamNavList.h"
#include "hamobj/HamNavProvider.h"
#include "hamobj/HamPartyJumpData.h"
#include "hamobj/HamPhotoDisplay.h"
#include "hamobj/HamPhraseMeter.h"
#include "hamobj/HamProviderPrinter.h"
#include "hamobj/HamRibbon.h"
#include "hamobj/HamScrollSpeedIndicator.h"
#include "hamobj/HamSkeletonConverter.h"
#include "hamobj/HamSupereasyData.h"
#include "hamobj/HamVisDir.h"
#include "hamobj/HamWardrobe.h"
#include "hamobj/HollaBackMinigame.h"
#include "hamobj/MeterDisplay.h"
#include "hamobj/MiniLeaderboardDisplay.h"
#include "hamobj/MoveDir.h"
#include "hamobj/OriginalChoreoRemixer.h"
#include "hamobj/PhotoSpotlightPositioner.h"
#include "hamobj/PracticeOptionsProvider.h"
#include "hamobj/PracticeSection.h"
#include "hamobj/RhythmBattle.h"
#include "hamobj/RhythmBattlePlayer.h"
#include "hamobj/RhythmDetector.h"
#include "hamobj/RhythmDetectorGroup.h"
#include "hamobj/SongDifficultyDisplay.h"
#include "hamobj/SongLayout.h"
#include "hamobj/StarsDisplay.h"
#include "hamobj/SuperEasyRemixer.h"
#include "hamobj/TransConstraint.h"
#include "obj/Data.h"
#include "obj/DataUtl.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/System.h"

PropertyEventProvider *TheHamProvider;

void HamTerminate() {
    DataArray *dataMacro = DataGetMacro("INIT_HAM");
    if (dataMacro) {
        ObjectDir::Terminate();
        GestureTerminate();
    }
}

void HamInit() {
    GestureInit();
#ifdef HX_NATIVE
    printf("DC3 Native: HamInit: INIT_HAM macro = %p\n", (void*)DataGetMacro("INIT_HAM"));
    fflush(stdout);
#endif
    if (DataGetMacro("INIT_HAM")) {
        REGISTER_OBJ_FACTORY(CharFeedback);
        CamShotCatVOInit();
        REGISTER_OBJ_FACTORY(DancerSequence);
        REGISTER_OBJ_FACTORY(HamBattleData);
        REGISTER_OBJ_FACTORY(SongLayout);
        REGISTER_OBJ_FACTORY(HamDriver);
        REGISTER_OBJ_FACTORY(Hmx::Object);
        HamGameData::Init();
        REGISTER_OBJ_FACTORY(HamIconMan);
        REGISTER_OBJ_FACTORY(HamIKEffector);
        REGISTER_OBJ_FACTORY(HamIKSkeleton);
        HamLabel::Init();
        HamList::Init();
        REGISTER_OBJ_FACTORY(HamWardrobe);
        REGISTER_OBJ_FACTORY(HamDirector);
        REGISTER_OBJ_FACTORY(HamCamShot);
        REGISTER_OBJ_FACTORY(HamCamTransform);
        HamCharacter::Init();
        REGISTER_OBJ_FACTORY(HamRegulate);
        REGISTER_OBJ_FACTORY(HamRibbon);
        REGISTER_OBJ_FACTORY(HamMove);
        REGISTER_OBJ_FACTORY(HamSkeletonConverter);
        REGISTER_OBJ_FACTORY(HamSong);
        REGISTER_OBJ_FACTORY(HamSupereasyData);
        REGISTER_OBJ_FACTORY(PracticeSection);
        REGISTER_OBJ_FACTORY(HamVisDir);
        HamPhotoDisplay::Init();
        REGISTER_OBJ_FACTORY(HamPhraseMeter);
        MeterDisplay::Init();
        MiniLeaderboardDisplay::Init();
        SongCollision::Init();
        SongDifficultyDisplay::Init();
        StarsDisplay::Init();
        REGISTER_OBJ_FACTORY(TransConstraint);
        REGISTER_OBJ_FACTORY(HamListRibbon);
        REGISTER_OBJ_FACTORY(HamScrollSpeedIndicator);
        HamNavList::Init();
        HamNavProvider::Init();
        PracticeOptionsProvider::Init();
        PhotoSpotlightPositioner::Init();
        REGISTER_OBJ_FACTORY(RhythmDetector);
        REGISTER_OBJ_FACTORY(RhythmBattle);
        REGISTER_OBJ_FACTORY(RhythmBattlePlayer);
        REGISTER_OBJ_FACTORY(RhythmDetectorGroup);
        REGISTER_OBJ_FACTORY(HollaBackMinigame);
        REGISTER_OBJ_FACTORY(MoveGraph);
        REGISTER_OBJ_FACTORY(CrazeHollaback);
        REGISTER_OBJ_FACTORY(HamPartyJumpData);
        REGISTER_OBJ_FACTORY(DanceRemixer);
        REGISTER_OBJ_FACTORY(OriginalChoreoRemixer);
        REGISTER_OBJ_FACTORY(SuperEasyRemixer);
        REGISTER_OBJ_FACTORY(BustAMoveData);
        REGISTER_OBJ_FACTORY(PoseFatalities);
        MoveDir::Init();
        DifficultyInit();
        TheDebug.AddExitCallback(HamTerminate);
        if (SystemConfig("objects", "PropertyEventProvider", "types")
                ->FindArray("HamProvider", false)) {
            SystemConfig("ham_init")->ExecuteBlock(1);
            TheHamProvider =
                ObjectDir::Main()->Find<PropertyEventProvider>("hamprovider", false);
            static Symbol language("language");
            TheHamProvider->SetProperty(language, SystemLanguage());
            if (TheSpeechMgr) {
                static Symbol voice_available("voice_available");
                TheHamProvider->SetProperty(
                    voice_available, TheSpeechMgr->SpeechSupported()
                );
            }
            HamProviderPrinter *printer = new HamProviderPrinter(); // uhhh...
        }
        PreloadSharedSubdirs("ham");
    }
}
