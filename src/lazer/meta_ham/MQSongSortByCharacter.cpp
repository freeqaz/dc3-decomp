#include "MQSongSortByCharacter.h"

#include "hamobj/HamGameData.h"
#include "HamSongMgr.h"
#include "MQSongSortNode.h"

int MQSongCharCmp::Compare(const NavListItemSortCmp *cmp, NavListNodeType type) const {
    switch (type) {
    case kNodeShortcut:
        return 0;

    case kNodeHeader: {
        const MQSongCharCmp *mqCmp = cmp->GetMQSongCharCmp();
        return strcmp(mCharacterName, mqCmp->mCharacterName);
    }

    case kNodeItem: {
        const MQSongCharCmp *mqCmp = cmp->GetMQSongCharCmp();
        return strcmp(mSongName, mqCmp->mSongName);
    }
    default:
        MILO_FAIL("invalid type of node comparison.\n");
    }
    return 0;
}

NavListHeaderNode *MQSongSortByCharacter::NewHeaderNode(NavListItemNode *node) const {
    auto cmp = node->GetCmp()->GetMQSongCharCmp();
    const char *songName = cmp->mSongName;
    const char *characterName = cmp->mCharacterName;
    MQSongCharCmp *songCharCmp = new MQSongCharCmp(songName, characterName);
    Symbol sym(MakeString("mqheader_%s", characterName));
    return new MQSongHeaderNode(songCharCmp, sym, true);
}

NavListShortcutNode *MQSongSortByCharacter::NewShortcutNode(NavListItemNode *node) const {
    const char *songName = node->GetCmp()->GetMQSongCharCmp()->mSongName;
    const char *characterName = node->GetCmp()->GetMQSongCharCmp()->mCharacterName;
    MQSongCharCmp *songCharCmp = new MQSongCharCmp(songName, characterName);
    return new NavListShortcutNode(songCharCmp, characterName, true);
}

NavListItemNode *MQSongSortByCharacter::NewItemNode(void *songSymbol) const {
    Symbol *pSongSymbol = static_cast<Symbol *>(songSymbol);
    Symbol outfitChar = GetOutfitCharacter(
        TheHamSongMgr.Data(TheHamSongMgr.GetSongIDFromShortName(*pSongSymbol, true))->Outfit(),
        true);
    MQSongCharCmp *songCharCmp = new MQSongCharCmp(pSongSymbol->Str(), outfitChar.Str());
    return new MQSongSortNode(songCharCmp, *pSongSymbol, outfitChar);
}