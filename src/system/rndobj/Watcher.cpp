#include "rndobj/Watcher.h"
#include "obj/Data.h"
#include "obj/DataFile.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"

// Guard to prevent recursive saves when loading watches from file
static bool savebool;

void Watcher::SaveWatches() {
    if (!savebool) {
        DataArrayPtr ptr;
        ptr->Resize(mWatches.size());
        int i = 0;
        for (std::vector<std::pair<DataArray *, DataNode> >::iterator it =
                 mWatches.begin();
             it != mWatches.end();
             ++it, ++i) {
            ptr->Node(i) = it->first;
        }
        DataWriteFile("watches.dta", ptr, 0);
    }
}

void Watcher::Update() {
    if (mWatches.size()) {
        mOverlay->SetLines(mWatches.size() * 2);
        int i = 0;
        for (std::vector<std::pair<DataArray *, DataNode> >::iterator it =
                 mWatches.begin();
             it != mWatches.end();
             ++it, ++i) {
            *mOverlay << i;
            *mOverlay << ": ";
            it->first->Print(*mOverlay, kDataArray, true, 0);
            *mOverlay << "\n";
            TheDebug.SetTry(true);
            it->second = it->first->Execute(false);
            TheDebug.SetTry(false);
            it->second.Print(*mOverlay, false, 0);
            *mOverlay << "\n";
        }
    } else
        mOverlay->Clear();
}

void Watcher::Remove(int i) {
    if (i < mWatches.size()) {
        mWatches.erase(mWatches.begin() + i);
        SaveWatches();
    }
}

DataNode Watcher::OnRemove(const DataArray *a) {
    Remove(a->Int(2));
    return 0;
}

void Watcher::Add(DataArray *a) {
    mWatches.push_back(std::pair<DataArray *, DataNode>(a, 0));
    SaveWatches();
}

DataNode Watcher::OnAdd(const DataArray *a) {
    Add(a->Array(2));
    return 0;
}

void Watcher::LoadWatches() {
    savebool = true;
    mWatches.clear();
    DataArray *watchArr = DataReadFile("watches.dta", false);
    if (watchArr) {
        for (int i = 0; i < watchArr->Size(); i++) {
            Add(watchArr->Array(i));
        }
    }
    savebool = false;
}

void Watcher::Init() {
    SetName("watcher", ObjectDir::Main());
    LoadWatches();
}

BEGIN_HANDLERS(Watcher)
    HANDLE(add, OnAdd)
    HANDLE(remove, OnRemove)
    HANDLE_ACTION(reload, LoadWatches())
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
