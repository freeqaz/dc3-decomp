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
        return strcmp(unk8, mqCmp->unk8);
    }

    case kNodeItem: {
        const MQSongCharCmp *mqCmp = cmp->GetMQSongCharCmp();
        return strcmp(unk4, mqCmp->unk4);
    }
    default:
        MILO_FAIL("invalid type of node comparison.\n");
    }
    return 0;
}

NavListHeaderNode *MQSongSortByCharacter::NewHeaderNode(NavListItemNode *node) const {
    auto cmp = node->GetCmp()->GetMQSongCharCmp();
    const char *p1 = cmp->unk4;
    const char *p2 = cmp->unk8;
    MQSongCharCmp *songCharCmp = new MQSongCharCmp(p1, p2);
    Symbol sym(MakeString("mqheader_%s", p2));
    return new MQSongHeaderNode(songCharCmp, sym, true);
}

NavListShortcutNode *MQSongSortByCharacter::NewShortcutNode(NavListItemNode *node) const {
    const char *p1 = node->GetCmp()->GetMQSongCharCmp()->unk4;
    const char *p2 = node->GetCmp()->GetMQSongCharCmp()->unk8;
    MQSongCharCmp *songCharCmp = new MQSongCharCmp(p1, p2);
    return new NavListShortcutNode(songCharCmp, p2, true);
}

NavListItemNode *MQSongSortByCharacter::NewItemNode(void *node) const {
    Symbol outfitChar;
    Symbol shortName = *static_cast<Symbol *>(node);
    outfitChar = GetOutfitCharacter(
        TheHamSongMgr.Data(TheHamSongMgr.GetSongIDFromShortName(shortName, true))->Outfit(),
        true);
    MQSongCharCmp *songCharCmp = new MQSongCharCmp(shortName.Str(), outfitChar.Str());
    return new MQSongSortNode(songCharCmp, shortName, outfitChar);
}