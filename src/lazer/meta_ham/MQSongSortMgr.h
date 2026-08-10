#pragma once
#include "MQSongSort.h"
#include "NavListSortMgr.h"
#include "ui\UIListProvider.h"

class MQSongSortMgr : public NavListSortMgr {
public:
    static void Init(SongPreview &);
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SelectionIs(Symbol);
    virtual Symbol MoveOn();
    virtual void OnEnter();

    void UpdateList();
    bool IsCharacter(Symbol) const;
    bool IsSong(Symbol) const;

    std::map<Symbol, std::vector<Symbol> > GetCharacterSongs() { return mCharacterSongs; }
    std::map<Symbol, std::vector<Symbol> > &CharacterSongs() { return mCharacterSongs; }
    std::vector<Symbol> &GetVecAt(Symbol s) { return mCharacterSongs[s]; }
    std::vector<Symbol> &GetFlatList() { return mFlatList; }

private:
    MQSongSortMgr(SongPreview &sp);
    virtual ~MQSongSortMgr();

protected:
    // std::set<Symbol> unk78; // 0x78
    std::map<Symbol, std::vector<Symbol> > mCharacterSongs; // 0x78
    // double *unk7c; // 0x7c
    // double *unk80;
    // double *unk84;
    // double *unk88;
    // bool unk8c; // 0x8c
    std::vector<Symbol> mFlatList; // 0x90
};

extern MQSongSortMgr *TheMQSongSortMgr;
