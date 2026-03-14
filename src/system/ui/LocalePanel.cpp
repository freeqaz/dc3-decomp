#include "ui/LocalePanel.h"
#include "ui/PanelDir.h"
#include "ui/UI.h"
#include "ui/UIList.h"
#include "ui/UIListLabel.h"
#include "utl/Locale.h"
#include "utl/MakeString.h"
#include "utl/Std.h"
#include <algorithm>

namespace {
    struct LabelSort {
        bool operator()(const UILabel *u1, const UILabel *u2) const {
            return stricmp(u1->GetTextToken().Str(), u2->GetTextToken().Str()) < 0;
        }
    };
}

int LocalePanel::NumData() const { return mEntries.size(); }

bool LocalePanel::IsActive(int i) const { return !mEntries[i].mLabel.empty(); }

float LocalePanel::GapSize(int, int i, int, int) const {
    if (IsActive(i))
        return 30.0f;
    else
        return 0;
}

UIScreen *LocalePanel::Screen() {
    int i = TheUI->PushDepth();
    return TheUI->ScreenAtDepth(i - 1);
}

Symbol LocalePanel::TokenForLabel(UILabel *label) {
    if (label->TextToken().Null()) {
        return "<no token>";
    } else {
        bool b18 = false;
        Localize(label->TextToken(), &b18, TheLocale);
        if (b18)
            return label->TextToken();
        else
            return "<token not found>";
    }
}

void LocalePanel::AddHeading(const char *cc) {
    Entry entry;
    entry.mHeading = cc;
    mEntries.push_back(entry);
}

void LocalePanel::Enter() {
    mEntries.clear();
    UIScreen *screen = Screen();
    std::list<PanelRef> &panelList = screen->PanelList();
    for (std::list<PanelRef>::iterator it = panelList.begin();
         it != panelList.end();
         ++it) {
        AddDirEntries((ObjectDir *)it->mPanel->LoadedDir(), it->mPanel->Name());
    }
    UIPanel::Enter();
}

void LocalePanel::Text(int i, int j, UIListLabel *listlabel, UILabel *label) const {
    Entry *entry = (Entry *)&mEntries[j];
    if (listlabel->Matches("heading")) {
        label->SetEditText(entry->mHeading.c_str());
    } else if (listlabel->Matches("label")) {
        label->SetEditText(entry->mLabel.c_str());
    } else if (listlabel->Matches("token")) {
        label->SetEditText(entry->mToken.Str());
    } else if (listlabel->Matches("string")) {
        label->SetEditText(entry->mString.c_str());
    }
}

LocalePanel::Entry::Entry() {}

void LocalePanel::AddDirEntries(ObjectDir *dir, const char *cc) {
    std::vector<UILabel *> labels;
    for (ObjDirItr<UILabel> it(dir, true); it != nullptr; ++it) {
        if (it->Showing())
            labels.push_back(it);
    }
    std::sort(labels.begin(), labels.end(), LabelSort());
    if (!labels.empty()) {
        AddHeading(MakeString("%s: %s", cc ? cc : "proxy", PathName(dir)));
    }
    for (std::vector<UILabel *>::iterator it = labels.begin(); it != labels.end(); ++it) {
        UILabel *cur = *it;
        Entry entry;
        entry.mLabel = cur->Name();
        entry.mToken = TokenForLabel(cur);
        entry.mString = cur->GetText();
        mEntries.push_back(entry);
    }
    for (ObjDirItr<UIList> it(dir, true); it != nullptr; ++it) {
        if (it->Showing()) {
            AddHeading(MakeString("%s: %s", it->ClassName(), it->Name()));
            const std::vector<UIListWidget *> &widgets = it->GetWidgets();
            std::vector<UIListWidget *>::const_iterator wIt;
            for (int i = 0; i < it->NumDisplay(); i++) {
                for (wIt = widgets.begin(); wIt != widgets.end(); ++wIt) {
                    UIListLabel *listLabel = dynamic_cast<UIListLabel *>(*wIt);
                    if (listLabel) {
                        UILabel *elementLabel = listLabel->ElementLabel(i);
                        if (elementLabel) {
                            if (elementLabel->GetText().length() != 0) {
                                Entry entry;
                                entry.mLabel =
                                    MakeString("%i:%s", i, listLabel->MatchName());
                                entry.mToken = TokenForLabel(elementLabel);
                                entry.mString = elementLabel->GetText();
                                mEntries.push_back(entry);
                            }
                        }
                    }
                }
            }
        }
    }
    for (ObjDirItr<PanelDir> it(dir, true); it != nullptr; ++it) {
        if (it != dir) {
            AddDirEntries(it, 0);
        }
    }
}

BEGIN_HANDLERS(LocalePanel)
    HANDLE_EXPR(token, mEntries[_msg->Int(2)].mToken)
    HANDLE_EXPR(screen, Screen())
    HANDLE_SUPERCLASS(UIPanel)
END_HANDLERS
