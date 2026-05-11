#include "meta_ham/NavListSort.h"
#include "math/Utl.h"
#include "meta_ham/NavListNode.h"
#include "math/Utl.h"
#include "obj/Data.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "ui/UI.h"
#include "utl/Locale.h"

struct NodeFind {
    NodeFind(Symbol t) : token(t) {}
    bool operator()(const NavListNode *n) const { return n->GetToken() == token; }

    Symbol token;
};

NavListSort::NavListSort() : mShortcutNodes(0), mHighlightNode(0), mPrevHighlightNode(0) {}

BEGIN_HANDLERS(NavListSort)
    HANDLE_EXPR(get_current_shortcut, GetCurrentShortcut())
    HANDLE_ACTION(select_shortcut, OnSelectShortcut(_msg->Int(2)))
    HANDLE_ACTION(highlight_next_header, ChangeHighlightHeader(1))
    HANDLE_ACTION(highlight_prev_header, ChangeHighlightHeader(-1))
    HANDLE_EXPR(sort_name, mSortName)
    HANDLE_EXPR(highlight_shortcut_str, HighlightTokenStr())
END_HANDLERS

void NavListSort::DeleteItemList() {
    mList.clear();
#ifdef HX_NATIVE
    for (auto it = mAllNodes.begin(); it != mAllNodes.end(); ++it) {
        if (*it) {
            (*it)->DeleteAll();
            RELEASE(*it);
        }
    }
#else
    FOREACH (it, mAllNodes) {
        (*it)->DeleteAll();
        RELEASE(*it);
    }
#endif
    mAllNodes.clear();
}

void NavListSort::UpdateHighlight() {
    MILO_ASSERT(mList.size() && mList[0], 0xE1);
    int i6 = -1;
    mPrevHighlightNode = mHighlightNode;
    if (!mHighlightNode) {
        mHighlightNode = mList[0];
    } else {
        i6 = mHighlightNode->StartIndex();
    }
    int idx = mHighlightNode->StartIndex();
    while (!mHighlightNode->IsActive()) {
        idx = (idx + 1) % mList.size();
        if (i6 == idx)
            break;
        mHighlightNode = mList[idx];
    }
}

void NavListSort::OnSelectShortcut(int idx) {
    mHighlightNode = mShortcutNodes[idx]->GetFirstActive();
    static Symbol skip_to_ix("skip_to_ix");
    static Message msg(skip_to_ix, 0);
    msg[0] = mHighlightNode->StartIndex();
    TheUI->Handle(msg, false);
}

const char *NavListSort::HighlightTokenStr() const {
    NavListShortcutNode *shortcut = mHighlightNode->GetShortcut();
    if (shortcut->LocalizeToken()) {
        return Localize(shortcut->GetToken(), nullptr, TheLocale);
    } else {
        return shortcut->GetToken().Str();
    }
}

NavListSortNode *NavListSort::GetNode(Symbol s) const {
    FOREACH (it, mList) {
        if (s == (*it)->GetToken())
            return *it;
    }
    return nullptr;
}

int NavListSort::GetDataCount() const { return mList.size(); }

void NavListSort::DeleteTree() {
    mPrevHighlightNode = 0;
    mHighlightNode = 0;
    mList.clear();
    FOREACH (it, mShortcutNodes) {
        (*it)->DeleteAll();
        RELEASE(*it);
    }
    mShortcutNodes.clear();
}

bool NavListSort::SetHighlightID(DataArray *a) {
    NavListSortNode *prevHighlight = mHighlightNode;
    mHighlightNode = nullptr;
    mPrevHighlightNode = prevHighlight;
    int arraySize = a->Size();
    if (arraySize == 0)
        return false;
    if (arraySize == 1) {
        auto nodeIt = std::find_if(mAllNodes.begin(), mAllNodes.end(), NodeFind(a->Sym(0)));
        if (nodeIt == mAllNodes.end())
            return false;
        mHighlightNode = *nodeIt;
        return true;
    }
    auto& shortcuts = mShortcutNodes;
    auto shortcutIt = std::find_if(shortcuts.begin(), shortcuts.end(), NodeFind(a->Sym(0)));
    if (shortcutIt == shortcuts.end())
        return false;
    MILO_ASSERT(kNodeShortcut == (*shortcutIt)->GetType(), 0x44);
    const std::list<NavListSortNode *> &children = (*shortcutIt)->Children();
    auto headerIt = std::find_if(children.begin(), children.end(), NodeFind(a->Sym(1)));
    if (headerIt == children.end())
        return false;
    MILO_ASSERT(kNodeHeader == (*headerIt)->GetType(), 0x4E);
    if (arraySize == 2) {
        mHighlightNode = *headerIt;
        return true;
    }
    const std::list<NavListSortNode *> &grandChildren = (*headerIt)->Children();
    auto itemIt = std::find_if(grandChildren.begin(), grandChildren.end(), NodeFind(a->Sym(2)));
    if (itemIt == grandChildren.end())
        return false;
    if (arraySize == 3) {
        mHighlightNode = *itemIt;
        return true;
    }
    const std::list<NavListSortNode *> &greatGrandChildren = (*itemIt)->Children();
    auto subItemIt = std::find_if(
        greatGrandChildren.begin(), greatGrandChildren.end(), NodeFind(a->Sym(3))
    );
    if (subItemIt == greatGrandChildren.end())
        return false;
    mHighlightNode = *itemIt;
    return true;
}

int NavListSort::GetCurrentShortcut() {
    if (!mHighlightNode)
        return 0;
    else {
        NavListShortcutNode *shortcut = mHighlightNode->GetShortcut();
        for (int i = 0; i < mShortcutNodes.size(); i++) {
            if (!mShortcutNodes[i]->Compare(shortcut, kNodeShortcut)) {
                return i;
            }
        }
        MILO_FAIL("Shortcut not found for this entry!\n");
        return -1;
    }
}

void NavListSort::ChangeHighlightHeader(int dir) {
    if (dir != 1 && dir != -1) {
        MILO_ASSERT(dir == 1 || dir == -1, 0xA0);
    }
    
    int shortcutIdx = GetCurrentShortcut();
    int nextIdx = shortcutIdx;
    
    NavListNodeType type = mHighlightNode->GetType();
    
    if (dir == 1) {
        if (type != kNodeFunction) {
            nextIdx = shortcutIdx + 1;
        }
    } else if (dir == -1) {
        if (type == kNodeFunction || type == kNodeHeader || 
            (type == kNodeItem && !GetHeaderSelectable() && mHighlightNode == mShortcutNodes[shortcutIdx]->GetFirstActive())) {
            nextIdx = shortcutIdx - 1;
        }
    }
    
    nextIdx = Mod(nextIdx, mShortcutNodes.size());
    
    NavListShortcutNode *shortcutNode = mShortcutNodes[nextIdx];
    while (!shortcutNode->IsActive()) {
        nextIdx += dir;
        nextIdx = Mod(nextIdx, mShortcutNodes.size());
        shortcutNode = mShortcutNodes[nextIdx];
    }
    
    OnSelectShortcut(nextIdx);
}
