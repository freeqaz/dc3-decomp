#include "flow\PropertyEventListener.h"
#include "flow\FlowNode.h"
#include "flow\DrivenPropertyEntry.h"
#include "obj/Object.h"

PropertyEventListener::PropertyEventListener(Hmx::Object *owner)
    : mAutoPropEntries(owner), mEventsRegistered(0) {}

void PropertyEventListener::RegisterEvents(FlowNode *node) {
    static Symbol reactivate("reactivate");
    if (!mEventsRegistered) {
        if (!mAutoPropEntries.empty()) {
            for (ObjVector<AutoPropEntry>::iterator it = mAutoPropEntries.begin();
                 it != mAutoPropEntries.end();
                 ++it) {
                if (it->mProvider) {
                    it->mProvider->AddPropertySink(node, it->mPropertyArray, reactivate);
                }
            }
        }
    }
    mEventsRegistered = true;
}

void PropertyEventListener::UnregisterEvents(FlowNode *node) {
    if (mEventsRegistered) {
        if (!mAutoPropEntries.empty()) {
            for (ObjVector<AutoPropEntry>::iterator it = mAutoPropEntries.begin();
                 it != mAutoPropEntries.end();
                 ++it) {
                if (it->mProvider) {
                    it->mProvider->RemovePropertySink(node, it->mPropertyArray);
                }
            }
        }
    }
    mEventsRegistered = false;
}

void PropertyEventListener::GenerateAutoNames(FlowNode *node, bool clear) {
    if (clear) {
        mAutoPropEntries.clear();
    }

    const auto &entries = node->DrivenPropEntries();
    FOREACH (entry_it, entries) {
        // DrivenObj() resolves through FlowPtr::Get(), which is non-const.
        // Spelled inline rather than bound to a local: the target re-reads the
        // vector bounds off the DrivenPropertyEntry pointer every iteration
        // (lwz r30, 0x8(r29) / lwz r11, 0xc(r29)) instead of pinning entry+0x8.
        FOREACH (op_it, const_cast<ObjVector<FlowMathOp> &>(entry_it->MathOps())) {
            // An op whose FlowPtr does not resolve is SKIPPED, and the entry
            // that is appended is filled in rather than left blank:
            //   82428B2C  addi r3, r30, 0x18   ; &op->mDrivenObj
            //   82428B30  bl   FlowPtr<Hmx::Object>::Get
            //   82428B34  mr.  r4, r3
            //   82428B38  beq  .L_82428BB8     ; -> next op, nothing pushed
            //   82428B70  bl   SetObjConcrete  ; entry.mProvider = that object
            //   82428B78  addi r3, r30, 0x10   ; &op->mRhs
            //   82428B7C  bl   DataNode::Array
            //   82428B80  stw  r3, 0x60(r31)   ; entry.mPropertyArray = it
            //   82428B8C  bl   ObjVector<AutoPropEntry>::push_back
            // We pushed a default-constructed entry for every op, so
            // mAutoPropEntries carried one null-provider, null-array row per
            // math op and RegisterEvents/UnregisterEvents skipped every one of
            // them -- no property sink was ever registered.
            Hmx::Object *provider = op_it->DrivenObj();
            if (provider) {
                AutoPropEntry entry(node);
                entry.mProvider = provider;
                entry.mPropertyArray = op_it->Rhs().Array(nullptr);
                mAutoPropEntries.push_back(entry);
            }
        }
    }
}
