#pragma once
#include "math/Mtx.h"
#include "math/Vec.h"
#include "obj/Object.h"
#include "rndobj/Mesh.h"
#include "ui/UIColor.h"
#include "ui/UIComponent.h"
#include "ui/UIListState.h"
#include "utl/MemMgr.h"

class UIList;
class UIListProvider;

enum UIListWidgetState {
    kUIListWidgetActive,
    kUIListWidgetHighlight,
    kUIListWidgetInactive,
    kNumUIListWidgetStates
};

struct UIListElementDrawState {
    bool mActive; // 0x0
    float mPosX; // 0x4
    float mPosY; // 0x8
    float mPosZ; // 0xc
    float mAlpha; // 0x10
    UIListWidgetState mElementState; // 0x14
    UIComponent::State mComponentState; // 0x18
    // DC3 additions: unk1c-unk20 absorb HamListRibbon overlay Color write
    // (*(Color*)&mElementState = color), preventing it from clobbering mDisplay/mShowing
    int unk1c; // 0x1c
    int unk20; // 0x20
    int unk24; // 0x24
    int mRibbonInteraction; // 0x28 - gesture state flag, cleared by HamNavList
    int mNavVtablePtr; // 0x2c - secondary vtable ptr, set by HamNavList
    int mDisplay; // 0x30
    int mShowing; // 0x34
    int mData; // 0x38
}; // size: 0x3c

struct UIListWidgetDrawState {
    Vector3 mFirstPos; // 0x0 (padded to 0x10)
    Vector3 mLastPos; // 0x10 (padded to 0x20)
    Vector3 mHighlightPos; // 0x20 (padded to 0x30)
    int mHighlightDisplay; // 0x30
    UIListWidgetState mHighlightElementState; // 0x34
    std::vector<UIListElementDrawState> mElements; // 0x38
};

enum UIListWidgetDrawType {
    kUIListWidgetDrawAlways,
    kUIListWidgetDrawOnlyFocused,
    kUIListWidgetDrawFocusedOrManual,
    kNumUIListWidgetDrawTypes
};

enum DrawCommand {
    kDrawAll,
    kDrawFirst,
    kExcludeFirst
};

class UIListWidget : public Hmx::Object {
public:
    // Hmx::Object
    OBJ_CLASSNAME(UIListWidget)
    OBJ_SET_TYPE(UIListWidget)
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);
    virtual void Save(BinStream &);
    virtual void Copy(const Hmx::Object *, Hmx::Object::CopyType);
    virtual void Load(BinStream &);
    // UIListWidget
    virtual UIList *SubList(int) { return nullptr; }
    virtual void ResourceCopy(const UIListWidget *);
    virtual void CreateElements(UIList *, int) {}
    virtual void Draw(
        const UIListWidgetDrawState &,
        const UIListState &,
        const Transform &,
        UIComponent::State,
        Box *,
        DrawCommand
    ) {}
    virtual void Fill(const UIListProvider &, int, int, int) {}
    virtual void StartScroll(int, bool) {}
    virtual void CompleteScroll(const UIListState &, int) {}
    virtual void Poll() {}

    float DrawOrder() const;
    float DisabledAlphaScale() const { return mDisabledAlphaScale; }
    UIListWidgetDrawType WidgetDrawType() const { return mWidgetDrawType; }
    UIList *ParentList() { return mParentList; }
    void SetParentList(UIList *);
    void SetColor(UIListWidgetState, UIComponent::State, UIColor *);

    NEW_OBJ(UIListWidget)
    OBJ_MEM_OVERLOAD(0x48)

protected:
    UIListWidget();

    void CalcXfm(const Transform &, const Vector3 &, Transform &);
    UIColor *DisplayColor(UIListWidgetState, UIComponent::State) const;
    void
    DrawMesh(RndMesh *, UIListWidgetState, UIComponent::State, Transform const &, Box *);

    float mDrawOrder; // 0x2c
    float mDisabledAlphaScale; // 0x30
    ObjPtr<UIColor> mDefaultColor; // 0x34
    std::vector<std::vector<ObjPtr<UIColor> > > mColors; // 0x48 - a vector of vectors of
                                                         // ObjPtrs...wonderful
    UIListWidgetDrawType mWidgetDrawType; // 0x54
    UIList *mParentList; // 0x58
};
