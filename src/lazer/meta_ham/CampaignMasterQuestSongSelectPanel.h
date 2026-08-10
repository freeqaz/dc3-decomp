#pragma once
#include "CampaignSongSelectPanel.h"
#include "TexLoadPanel.h"
#include "meta_ham\MQSongSortMgr.h"
#include "obj\Data.h"
#include "obj\Object.h"
#include "os\DateTime.h"
#include "ui\UILabel.h"
#include "ui\UIListProvider.h"
#include "utl\Symbol.h"

class CampaignMasterQuestSongSelectPanel : public TexLoadPanel {
public:
    CampaignMasterQuestSongSelectPanel();
    virtual ~CampaignMasterQuestSongSelectPanel();
    OBJ_CLASSNAME(CampaignMasterQuestSongSelectPanel)
    OBJ_SET_TYPE(CampaignMasterQuestSongSelectPanel)
    virtual DataNode Handle(DataArray *, bool);
    virtual void Enter();
    virtual void Poll();
    virtual void Unload();
    virtual void Load();
    virtual void FinishLoad();

    NEW_OBJ(CampaignMasterQuestSongSelectPanel)

    bool CanSelectSong(int);
    Symbol GetSong(int);
    int GetTimeSinceEnter() const;
    Symbol GetSelectedSong();
    void OnHighlightSong();
    bool CanSelectCurrentSong();
    bool IsCurrentSelectionSong();
    void Refresh();
    void OnHighlightHeader();
    void SelectSong();

private:
    // size 0xc
    class Impl {
    public:
        Impl() {}
        void Init(CampaignMasterQuestSongSelectPanel *);

        UILabel *mContextualTitleLabel; // 0x0
        UILabel *mContextualInstructionsLabel; // 0x4
        UILabel *mContextualStarsLabel; // 0x8
    };

    MQSongSortMgr *m_pCampaignSongProvider; // 0x54
    DateTime mEnterTime; // 0x58
    bool mPreviewDelayFinished; // 0x5e
    Impl *mImpl; // 0x60
};
