#include "flow\FlowManager.h"
#include "flow\FlowNode.h"
#include "obj\ObjPtrVec_impl.h"
#include "obj\Data.h"
#include "os\Timer.h"
#include "rndobj\Env.h"
#include "rndobj\Overlay.h"
#include "rndobj\Trans.h"
#include "utl\MakeString.h"

// w8-e 2026-09-15: `merged_ObjPtrVecErase` (240 B, 0%) is NOT a target symbol.
// It is a SYNTHETIC ICF-group label invented by our own tooling:
// build/373307D9/icf_aliases.map:11045 declares the group at 823EA0B8 and lists
// four real members folded there -- ObjPtrVec<FlowNode|RndEnviron|RndLight|
// SpotlightDrawer, ObjectDir>::erase -- all tagged `icf_aliases.synthetic`, and
// config/373307D9/symbols.txt:123849 binds the made-up name to that address.
// Nothing our source can spell is named `merged_ObjPtrVecErase`, so this row
// can never pair and can never score.  The FlowNode spelling this TU really does
// emit (see the explicit instantiation above) is one of the four folded copies.
// Measured 0.0% and structurally unscoreable; not a missing body.
//
// w8-k 2026-09-15: RE-ANCHORED, and the row is now scoreable at 80.58%.
// Everything above is accurate about the artifact; "can never pair and can never
// score" was the one step too far.  A synthetic label is not a fact about the
// image, it is a choice in config/373307D9/symbols.txt, and the choice was
// wrong: ham_xbox_r.map names four REAL claimants at 0x823EA0B8, and the
// address-range test picks one without appealing to symbols.txt at all --
// splits.txt gives FlowManager .text [0x823E8318, 0x823EB8A0), which CONTAINS
// 0x823EA0B8, against LightPreset .text [0x8283DA50, 0x82850940), which does
// not, which is also why dtk carves the body into FlowManager.s.  symbols.txt
// now binds ?erase@?$ObjPtrVec@VFlowNode@@VObjectDir@@@@QAA?AViterator@1@V21@@Z
// there; LightPreset loses nothing, since its three spellings have no body in
// LightPreset.s either.  Whole-binary row diff: exactly 2 rows moved.
//
// RESIDUAL, 80.58% canonical / 80.0% raw, 240 B target vs 276 B base, 51 of 70
// instructions equal.  This is now ordinary decomp work, and the diagnosis is
// already done -- but the fix is NOT in this file and must not be made here:
//   * the target calls `bl ?Set@?$ObjPtrVec@VFlowNode@@VObjectDir@@@@QAAXViterator@1@PAVFlowNode@@@Z`;
//     we call ?SetObjConcrete@?$ObjRefConcrete@VFlowNode@@... directly, with a
//     10-instruction inserted cluster at rows 45-54 that is the `!obj &&
//     mListMode == 0` null-check from Set's body.  MSVC inlined Set into erase
//     for us and did not for retail.
//   * residual beyond that is one r26<->r27 and one r27<->r28 callee-saved
//     register-permutation pair, which the canonical ruler forgives anyway.
// ObjPtrVec<T1,T2>::erase and ::Set are both in src/system/obj/ObjPtr_p.h
// (erase at :878, Set at :313), which is PCH-REACHED -- it is in decomp_pch.h's
// 178-header closure -- so any edit there rebuilds and re-scores every
// ObjPtrVec<T> instantiation in the binary, far outside this unit.  Whoever
// takes it: make the change there, run a FULL ninja, and diff the whole binary,
// because the blast radius is the whole binary.

// w21-h (80.58, unchanged): the image is NOT uniform about inlining Set into
// erase -- three distinct erase bodies exist: FlowManager's (calls Set; ICF-folded
// with LightPreset's RndEnviron/RndLight/SpotlightDrawer copies), Font's RndMat
// and LightPreset's Spotlight (both inline Set).  Ours is the MIRROR image in
// two TUs: FlowManager inlines, LightPreset calls Set for all four types,
// CharClipGroup calls Set (image inlines).  In our objects the copy whose COMDAT
// is emitted FIRST of the erase/Set pair is the one that gets the other inlined
// (Font: erase sec 995 < Set 997, inlined; LightPreset: Set 1298 < erase 2214,
// not).  Measured inert here: dropping the explicit erase instantiation below,
// and replacing it with an explicit Set<FlowNode> instantiation -- section order
// (erase 563 < Set 585) and the inlining did not move.
// w18-d (80.58): an explicit instantiation of ObjPtrVec<FlowNode>::Set ahead of
// the erase instantiation does not stop Set being inlined into erase (inert).
// w24-c2 (80.58, BLOCKED-PCH): residual unchanged -- rows 39-54, the inlined
// Set body (10 inserted rows) where the image does `bl Set<FlowNode>`, plus an
// r26/r27/r28 permutation. New observation, not built: in the image's
// FlowManager.s the address order puts Set<FlowNode> right after
// _Vector_base::~_Vector_base and before Node::RefOwner / Node(const Node&),
// i.e. among the earliest ObjPtrVec<FlowNode> COMDATs, next to where our obj
// numbers Node(ObjRefOwner*) and const begin()/end() (secs 329-331); ours
// numbers Set at 589, after erase (579) and remove (581). So whatever made the
// original reference Set early is in the push_back/insert/operator= path of
// obj/ObjPtr_p.h / Object.h (all three call Set in the image), not in this
// file -- a PCH-reached edit with whole-binary blast radius.
template Hmx::Object *ObjPtrVec<RndTransformable, ObjectDir>::Node::RefOwner() const;
template ObjPtrVec<FlowNode, ObjectDir>::iterator
ObjPtrVec<FlowNode, ObjectDir>::erase(ObjPtrVec<FlowNode, ObjectDir>::iterator);
template void ObjPtrVec<RndEnviron, ObjectDir>::Set(
    ObjPtrVec<RndEnviron, ObjectDir>::iterator, RndEnviron *);

FlowManager *TheFlowMgr;

FlowManager::FlowManager() : unk2c(0), mExecuting(0), mPollables(this) {
    mFlowQueue.clear();
    mFrameCounterModulo = 0;
    mFrameTimeAccumulator = 0;
    mPeakFrameTime = 0;
    mLastFrameTime = 0;
    mElapsedTime = 0;
    for (int i = 0; i < 60; i++) {
        mFrameTimeSamples[i] = 0.0f;
    }
    mFlowOverlay = RndOverlay::Find("flow", false);
    mFlowPeakOverlay = RndOverlay::Find("flow_peak", false);
    mFlowTaskOverlay = RndOverlay::Find("flow_task", false);
    mFlowEventOverlay = RndOverlay::Find("flow_event", false);
}

FlowManager::~FlowManager() {}

void FlowManager::AddPollable(FlowNode *n) { mPollables.push_back(n); }
void FlowManager::RemovePollable(FlowNode *n) { mPollables.remove(n); }

void FlowManager::QueueCommand(FlowNode *n, FlowNode::QueueState q) {
    if (mExecuting && q != FlowNode::kQueueOne) {
        n->Execute(q);
    } else
        mFlowQueue[n] = q;
}

void FlowManager::CancelCommand(FlowNode *n) { mFlowQueue[n] = FlowNode::kImmediate; }

void FlowManager::AddEventTime(Symbol s, float f1) {
    float fsub = f1 - mElapsedTime;
    if (mEventTimes.find(s) != mEventTimes.end()) {
        DataNode &n = mEventTimes[s];
        float f7 = n.Array()->Float(0);
        int i5 = n.Array()->Int(1);
        float f8 = n.Array()->Float(2) + mElapsedTime;
        i5++;
        n.Array()->Node(0) = f7 + fsub;
        n.Array()->Node(1) = i5;
        n.Array()->Node(2) = f8;
    } else {
        DataArrayPtr ptr(fsub, 1, mElapsedTime);
        mEventTimes[s] = ptr;
    }
    mElapsedTime = 0;
}

void FlowManager::Poll() {
    float lastFrameAtEntry = mLastFrameTime;
    int numCommands = mFlowQueue.size();
    mLastFrameTime = 0;
    float eventTimeSum = 0.0f;
    float releaseMs = mFrameTimeAccumulator;
    Timer timer;
    timer.Reset();
    timer.Start();

    mExecuting = true;

    for (std::map<FlowNode *, FlowNode::QueueState>::iterator it = mFlowQueue.begin();
         it != mFlowQueue.end();
         ++it) {
        if (it->second != FlowNode::kImmediate) {
            it->first->Execute(it->second);
        }
    }
    mFlowQueue.clear();

    ObjPtrVec<FlowNode> polls(mPollables);
    FOREACH (it, polls) {
        (*it)->Execute(FlowNode::kWhenAble);
    }

    mExecuting = false;
    timer.Stop();
    float taskMs = mLastFrameTime + lastFrameAtEntry;
    unk2c = false;
    float timerMs = timer.Ms() - mLastFrameTime;

    Symbol peakSym(NULL);
    Symbol peakElapsedSym(NULL);
    float maxEventTime = -1.0f;
    float maxElapsedTime = -1.0f;

    if (!mEventTimes.empty()) {
        if (mFlowEventOverlay->Showing()) {
            *mFlowEventOverlay << "\n\n\n\n\n\n\n\n\n\n";
        }

        for (std::map<Symbol, DataNode>::iterator it = mEventTimes.begin();
             it != mEventTimes.end();
             ++it) {
            Symbol eventSym = it->first;
            DataNode node(it->second);
            float eventTime = node.Array()->Float(0);
            float elapsedTime = node.Array()->Float(2);

            eventTimeSum += eventTime;

            if (eventTime >= maxEventTime) {
                maxEventTime = eventTime;
                peakSym = eventSym;
            }
            if (elapsedTime >= maxElapsedTime) {
                maxElapsedTime = elapsedTime;
                peakElapsedSym = eventSym;
            }

            if (mFlowEventOverlay->Showing()) {
                float f2 = node.Array()->Float(2);
                float f0 = node.Array()->Float(0);
                int count = node.Array()->Int(1);
                *mFlowEventOverlay
                    << MakeString("%s    count: %i   time: %.3f ms   task: %.3f ms\n", eventSym.Str(), count, f0, f2);
            }
        }

        if (mFlowOverlay->Showing()) {
            *mFlowOverlay << MakeString(
                "Worst:   FlowTime: %s  %.3f    TaskTime: %s  %.3f\n",
                peakSym.Str(),
                maxEventTime,
                peakElapsedSym.Str(),
                maxElapsedTime
            );
        }
    }

    float total = timerMs + eventTimeSum;
    total += mFrameTimeAccumulator;

    if (mFlowOverlay->Showing()) {
        *mFlowOverlay << MakeString(
            "Events: %.3f ms  %i Commands in %.3f ms  Release: %.3f ms  Tasks: %.3f ms\n",
            eventTimeSum,
            numCommands,
            timerMs,
            releaseMs,
            taskMs
        );
    }

    mFrameTimeSamples[mFrameCounterModulo] = total;
    mFrameCounterModulo++;

    if (total > mPeakFrameTime) {
        mPeakFrameTime = total;
        DataArrayPtr ptr(peakSym, maxEventTime, peakElapsedSym, maxElapsedTime, total);
        DataNode dn(ptr);
        mPeakFrameInfo = dn;
    }

    float peakFrameTime = mPeakFrameTime;
    if (mFrameCounterModulo >= 60) {
        mAvgFrameTime = 0;
        for (int i = 0; i < 60; i++) {
            mAvgFrameTime += mFrameTimeSamples[i];
        }
        mPeakFrameTime = 0;
        mFrameCounterModulo = 0;
        mAvgFrameTime *= (1.0f / 60.0f);

        if (mPeakFrameInfo.Type() == kDataArray) {
            DataArray *arr = mPeakFrameInfo.Array();
            float peakTime = arr->Float(1);
            if (peakTime > 0 && mFlowPeakOverlay->Showing()) {
                float pt = arr->Float(1);
                *mFlowPeakOverlay << MakeString("%s %.3f ms\n", arr->Sym(0), pt);
            }
            float peakElapsed = arr->Float(3);
            if (peakElapsed > 0 && mFlowTaskOverlay->Showing()) {
                float pe = arr->Float(3);
                *mFlowTaskOverlay << MakeString("%s %.3f ms\n", arr->Sym(2), pe);
            }
        }
    }

    if (mFlowOverlay->Showing()) {
        *mFlowOverlay
            << MakeString("Average: %.3f ms   Peak: %.3f ms    Frame: %.3f ms\n", mAvgFrameTime, peakFrameTime, total);
    }

    Timer *autoTimer = AutoTimer::GetTimer(Symbol("flow"));
    if (autoTimer) {
        autoTimer->SetLastMs(total);
    }

    mEventTimes.clear();
    mFrameTimeAccumulator = 0;
    mLastFrameTime = 0;
}
