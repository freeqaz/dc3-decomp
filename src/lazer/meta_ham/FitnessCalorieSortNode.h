#pragma once
#include "meta_ham\NavListNode.h"
#include "stl\_vector.h"
#include "ui\UILabel.h"
#include "ui\UIListLabel.h"
#include "utl\Symbol.h"

class FitnessCalorieSortNode : public NavListItemNode {
public:
    virtual DataNode Handle(DataArray *, bool);
    virtual Symbol GetToken() const;
    virtual Symbol OnSelect();
    virtual void Text(UIListLabel *, UILabel *) const;

    FitnessCalorieSortNode(NavListItemSortCmp *, int);
    int GetCalories() const { return mCalories; }

protected:
    int mCalories;
};

class FitnessCalorieHeaderNode : public NavListHeaderNode {
public:
    virtual ~FitnessCalorieHeaderNode() {}
    virtual DataNode Handle(DataArray *, bool);
    virtual Symbol OnSelect();
    virtual Symbol Select();
    virtual Symbol OnSelectDone();
    virtual void OnHighlight();
    virtual void OnUnHighlight();
    /** Inline in the original (`f i` in ham_xbox_r.map, ICF-folded at
     * 0x82B05A48 with the other *HeaderNode::GetItemCount overrides). */
    virtual int GetItemCount() { return mItemCount; }
    virtual NavListSortNode *GetFirstActive();
    virtual void Text(UIListLabel *, UILabel *) const;
    virtual bool IsActive() const;
    virtual void Renumber(std::vector<NavListSortNode *> &);
    virtual void UpdateItemCount(NavListItemNode *);
    virtual void SetCollapseStateIcon(bool) const;

    FitnessCalorieHeaderNode(NavListItemSortCmp *, Symbol, bool);

protected:
    int mItemCount; // 0x58
};
