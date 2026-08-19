#pragma once
#include "NavListNode.h"
#include "SongRecord.h"
#include "utl\Symbol.h"
class MQSongHeaderNode : public NavListHeaderNode {
public:
    MQSongHeaderNode(NavListItemSortCmp *, Symbol, bool);

    DataNode Handle(DataArray *, bool);
    virtual ~MQSongHeaderNode() {}
    virtual Symbol OnSelect();
    virtual Symbol Select();
    virtual Symbol OnSelectDone();
    virtual void OnHighlight();
    virtual void OnUnHighlight();
    /** Inline in the original (`f i` in ham_xbox_r.map, ICF-folded at
     * 0x82B05A48 with the other *HeaderNode::GetItemCount overrides). */
    virtual int GetItemCount() { return mChallengeCount; }
    virtual NavListSortNode *GetFirstActive();
    virtual void Text(UIListLabel *, UILabel *) const;
    virtual bool IsActive() const;
    virtual const char *GetAlbumArtPath();
    virtual void UpdateItemCount(NavListItemNode *);
    virtual void SetItemCountString(UILabel *) const;
    virtual void SetCollapseStateIcon(bool) const;
    virtual void Renumber(std::vector<NavListSortNode *> &);

    int mChallengeCount; // 0x58
    bool mHighlighted; // 0x5c
};

class MQSongSortNode : public NavListItemNode {
public:
    MQSongSortNode(NavListItemSortCmp *cmp, Symbol shortName, Symbol character)
        : NavListItemNode(cmp), mShortName(shortName), mCharacter(character) {}
    virtual ~MQSongSortNode();
    virtual DataNode Handle(DataArray *, bool);
    virtual Symbol GetToken() const { return mShortName; }
    virtual Symbol OnSelect();
    virtual void Text(UIListLabel *, UILabel *) const;
    virtual void Custom(UIListCustom *, Hmx::Object *) const;
    virtual const char *GetAlbumArtPath();

    void SetCharacter(Symbol c) { mCharacter = c; }
    Symbol GetCharacter() const { return mCharacter; }

protected:
    Symbol mShortName; // 0x48
    Symbol mCharacter; // 0x4C
};
