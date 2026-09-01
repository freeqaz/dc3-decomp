#include "synth_xbox\ExternalMic.h"
#include "os\Debug.h"
#include "obj\Data.h"
#include "obj\Object.h"
#include "utl\Symbol.h"
#include <string.h>
#include <vector>
#include "synth_xbox\Mic.h"
#include "xdk\xapilibi\handleapi.h"
#include "xdk\xapilibi\processthreadsapi.h"
#include "xdk\xapilibi\synchapi.h"
#include "xdk\xapilibi\xbox.h"
#include "xdk\xapilibi\winerror.h"

std::vector<ExternalMicClientProxy *> ExternalMicClientMgr::mMicMasters;
std::vector<unsigned long> ExternalMicClientMgr::mDevToMicMaster;
std::vector<unsigned long> ExternalMicClientMgr::mMicMasterToDev;
std::vector<MicXbox *> ExternalMicClientMgr::mAssocMicXbox;

namespace {
    unsigned long ExternalMicThreadEntry(void *v) {
        return reinterpret_cast<ExternalMic *>(v)->sampleProcessThread();
    }

    std::vector<ExternalMic *> gMics;
}

int ExternalMic::NumConnectedMics() {
    int count = 0;
    for (unsigned int i = 0; i < gMics.size(); i++) {
        if (gMics[i]->unk9) {
            count++;
        }
    }
    return count;
}

ExternalMicClientProxy *ExternalMicClientMgr::GetMasterForIndex(unsigned long deviceId) {
    unsigned long master = mDevToMicMaster[deviceId];
    if (master != -1) {
        return mMicMasters[master];
    }
    for (unsigned int i = 0; i < mMicMasters.size(); i++) {
        if (mMicMasters[i] == 0) {
            mMicMasters[i] = new ExternalMicClientProxy(i);
        }
        if (mMicMasterToDev[i] == -1) {
            mDevToMicMaster[deviceId] = i;
            mMicMasterToDev[i] = deviceId;
            return mMicMasters[i];
        }
    }
    return 0;
}

void ExternalMicClientMgr::Associate(int index, MicXbox *mic) { mAssocMicXbox[index] = mic; }

bool ExternalMicClientMgr::ConnectedForClient(const MicXbox *mic) {
    for (unsigned int i = 0; i < mAssocMicXbox.size(); i++) {
        if (mAssocMicXbox[i] == mic && mMicMasters[i] != 0 && mMicMasters[i]->mConnected) {
            return true;
        }
    }
    return false;
}

long ExternalMicClientProxy::OnMicConnected(unsigned long deviceId, bool b, const Symbol &name) {
    mConnected = true;
    MicXbox *mic = ExternalMicClientMgr::mAssocMicXbox[mIndex];
    if (mic) {
        mic->OnMicConnected(deviceId, b, name);
        return 0;
    }
    return 0x8000FFFF;
}

void ExternalMicClientMgr::AddAudio(
    unsigned long deviceId, unsigned char *data, unsigned long bytes
) {
    ExternalMicClientProxy *master = GetMasterForIndex(deviceId);
    if (master) {
        MicXbox *mic = mAssocMicXbox[master->mIndex];
        if (mic) {
            mic->AddData(data, bytes);
        }
    }
}

float ExternalMicClientMgr::GetRequiredGain(unsigned long deviceId) {
    ExternalMicClientProxy *master = GetMasterForIndex(deviceId);
    if (master) {
        MicXbox *mic = mAssocMicXbox[master->mIndex];
        if (mic) {
            return mic->GetGain();
        }
    }
    return 1.0f;
}

void ExternalMicClientMgr::OnMicDisconnected(unsigned long deviceId) {
    ExternalMicClientProxy *master = GetMasterForIndex(deviceId);
    if (master) {
        master->mConnected = false;
        MicXbox *mic = mAssocMicXbox[master->mIndex];
        if (mic) {
            mic->OnMicDisconnected();
        }
        if (mDevToMicMaster[deviceId] != -1) {
            mMicMasterToDev[mDevToMicMaster[deviceId]] = -1;
            mDevToMicMaster[deviceId] = -1;
        }
    }
}

ExternalMic::ExternalMic(unsigned long ul)
    : mDeviceId(ul), mQuit(false), unk9(false), mLastGain(-1.0f) {
    mThread = CreateThread(0, 0, ExternalMicThreadEntry, this, 4, 0);
    MILO_ASSERT(mThread, 0x6a);
    SetThreadPriority(mThread, 15);
    XSetThreadProcessor(mThread, 3);
    ResumeThread(mThread);
}

ExternalMic::~ExternalMic() {
    mQuit = true;
    WaitForSingleObject(mThread, -1);
    CloseHandle(mThread);
}

namespace {
    // Field names recovered from ExternalMic::sampleProcessThread, which builds
    // two of these per connected mic: +0x04 is handed to XMicRequestData as its
    // XOVERLAPPED*, +0x14 is the raw operator-new[] block whose +0x80 is +0x10,
    // and +0x28 is the owning ExternalMic (dataReadyEntry reads it).
    struct XMicData {
        DWORD deviceId; // 0x0
        XOVERLAPPED *pOverlapped; // 0x4
        DWORD numFrames; // 0x8
        DWORD stride; // 0xc
        unsigned char *pData; // 0x10
        unsigned char *pAlloc; // 0x14
        unsigned short aFrameSizes[8]; // 0x18
        ExternalMic *owner; // 0x28
    };
}

void ExternalMic::dataReady(unsigned long, unsigned long, _XOVERLAPPED *pOverlapped) {
    XMicData *data = (XMicData *)pOverlapped->dwCompletionContext;
    if (data) {
        unsigned char buf[2048] = {0};
        unsigned char *pSrc = data->pData;
        if (0 < data->numFrames) {
            unsigned int total = 0;
            unsigned short *pFrameSize = data->aFrameSizes;
            for (unsigned int i = 0; i < data->numFrames; i++) {
                unsigned short frameSize = *pFrameSize;
                if (frameSize != 0) {
                    if (frameSize & 1) {
                        MILO_LOG(
                            "Mic data frame length was odd: %x bytes; truncating last byte\n",
                            frameSize
                        );
                        frameSize = frameSize - 1;
                    }
                    memcpy(buf + total, pSrc, frameSize);
                    total += frameSize;
                }
                pSrc += data->stride;
                pFrameSize++;
            }
            if (total != 0) {
                ExternalMicClientMgr::AddAudio(mDeviceId, buf, total);
            }
        }
        if (XMicGetStatus(data->deviceId) == 2) {
            XMicRequestData(
                data->deviceId,
                data->numFrames,
                data->pData,
                data->aFrameSizes,
                data->pOverlapped
            );
        }
    }
}

namespace {
    void dataReadyEntry(unsigned long a, unsigned long b, _XOVERLAPPED *pOverlapped) {
        XMicData *data = (XMicData *)pOverlapped->dwCompletionContext;
        data->owner->dataReady(a, b, pOverlapped);
    }
}

unsigned long ExternalMic::sampleProcessThread() {
    DWORD deviceId = mDeviceId | 0x04000000;
    DWORD frameBytes = 0;
    XMIC_CAPABILITIES caps;
    XMicData first;
    XMicData second;
    while (!mQuit) {
        memset(&first, 0, sizeof(first));
        memset(&second, 0, sizeof(second));
        do {
            Sleep(10);
        } while (XMicGetStatus(deviceId) != 1 && !mQuit);
        unk9 = true;
        if (XMicGetCapabilities(deviceId, &caps) == 0) {
            DWORD multiMic = caps.dwFlags & 1;
            if (XMicStart(deviceId, 0x100, &frameBytes, 0) == 0) {
                long hr = gatherGainAttribs(deviceId);
                if (hr >= 0) {
                    static Symbol generic_usb("generic_usb");
                    Symbol micName = generic_usb;
                    DataArray *cfg = SystemConfig("synth", "mic_types", "xbox");
                    for (int i = 1; i < cfg->Size(); i++) {
                        DataArray *entry = cfg->Array(i);
                        DataArray *capsCfg = entry->FindArray("capabilities", true);
                        if (capsCfg->Int(1) == caps.dwFlags && capsCfg->Int(2) == caps.wFormatTag
                            && capsCfg->Int(3) == caps.nChannels
                            && capsCfg->Int(4) == caps.nSamplesPerSec
                            && capsCfg->Int(5) == caps.nBlockAlign
                            && capsCfg->Int(6) == caps.wBitsPerSample) {
                            DataArray *minGain = entry->FindArray("min_gain", true);
                            if (minGain->Float(1) == mGainLeft) {
                                DataArray *maxGain = entry->FindArray("max_gain", true);
                                if (maxGain->Float(1) == mGainRight) {
                                    micName = entry->Sym(0);
                                    break;
                                }
                            }
                        }
                    }
                    ExternalMicClientProxy *master =
                        ExternalMicClientMgr::GetMasterForIndex(mDeviceId);
                    if (master) {
                        hr = master->OnMicConnected(0x100, multiMic, micName);
                        if (hr >= 0) {
                            XOVERLAPPED firstOverlapped;
                            XOVERLAPPED secondOverlapped;
                            DWORD numFrames = multiMic ? 1 : 2;
                            first.deviceId = deviceId;
                            first.pOverlapped = &firstOverlapped;
                            first.numFrames = numFrames;
                            first.stride = frameBytes;
                            first.pAlloc = new unsigned char[numFrames * frameBytes + 0x100];
                            first.pData = first.pAlloc + 0x80;
                            first.owner = this;
                            second.deviceId = deviceId;
                            second.pOverlapped = &secondOverlapped;
                            second.numFrames = numFrames;
                            second.stride = frameBytes;
                            second.pAlloc = new unsigned char[numFrames * frameBytes + 0x100];
                            second.pData = second.pAlloc + 0x80;
                            second.owner = this;
                            firstOverlapped.dwExtendedError = 0;
                            firstOverlapped.hEvent = 0;
                            firstOverlapped.pCompletionRoutine = dataReadyEntry;
                            firstOverlapped.dwCompletionContext = (DWORD_PTR)&first;
                            secondOverlapped.dwExtendedError = 0;
                            secondOverlapped.hEvent = 0;
                            secondOverlapped.pCompletionRoutine = dataReadyEntry;
                            secondOverlapped.dwCompletionContext = (DWORD_PTR)&second;
                            if (XMicRequestData(
                                    first.deviceId,
                                    first.numFrames,
                                    first.pData,
                                    first.aFrameSizes,
                                    first.pOverlapped
                                ) != ERROR_IO_PENDING) {
                                hr = 0x80004005;
                            }
                            if (hr >= 0) {
                                if (XMicRequestData(
                                        second.deviceId,
                                        second.numFrames,
                                        second.pData,
                                        second.aFrameSizes,
                                        second.pOverlapped
                                    ) != ERROR_IO_PENDING) {
                                    hr = 0x80004005;
                                }
                                if (hr >= 0) {
                                    while (!mQuit) {
                                        SleepEx(10, true);
                                        hr = processGain(deviceId);
                                        MILO_ASSERT(SUCCEEDED(hr), 0x149);
                                        if (XMicGetStatus(deviceId) != 2) {
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        XMicStop(deviceId, 0);
        unk9 = false;
        ExternalMicClientMgr::OnMicDisconnected(mDeviceId);
        mLastGain = -1.0f;
        if (first.pAlloc) {
            delete[] first.pAlloc;
        }
        first.pData = 0;
        first.pAlloc = 0;
        if (second.pAlloc) {
            delete[] second.pAlloc;
        }
        second.pData = 0;
        second.pAlloc = 0;
    }
    return 0;
}

long ExternalMic::processGain(unsigned long deviceId) {
    float gainReq = ExternalMicClientMgr::GetRequiredGain(mDeviceId);
    if (gainReq != mLastGain) {
        MILO_ASSERT((0.0f <= gainReq) && (gainReq <= 1.0f), 0x26f);
        float gain = (mGainRight - mGainLeft) * gainReq + mGainLeft;
        DWORD result = XMicSetGain(deviceId, gain, 0);
        mLastGain = gainReq;
        if (result != 0) {
            return 0x80004005;
        }
    }
    return 0;
}

long ExternalMic::gatherGainAttribs(unsigned long deviceId) {
    if (XMicGetGain(deviceId, 1, &mGainLeft) > 0) {
        return 0x80004005;
    }
    DWORD result = XMicGetGain(deviceId, 2, &mGainRight);
    return 0 != result ? 0x80004005 : 0;
}

void ExternalMicClientMgr::Terminate() {
    for (unsigned int i = 0; i < mMicMasters.size(); i++) {
        if (mMicMasters[i] != 0) {
            delete mMicMasters[i];
        }
    }
    mMicMasters.clear();
}

void ExternalMic::Terminate() {
    for (unsigned int i = 0; i < gMics.size(); i++) {
        delete gMics[i];
    }
    gMics.clear();
    ExternalMicClientMgr::Terminate();
}

void ExternalMicClientMgr::Init() {
    mAssocMicXbox.reserve(4);
    mMicMasters.reserve(4);
    mDevToMicMaster.reserve(4);
    mMicMasterToDev.reserve(4);
    for (int i = 0; i < 4; i++) {
        mDevToMicMaster.push_back(-1);
        mMicMasterToDev.push_back(-1);
        mMicMasters.push_back(0);
        mAssocMicXbox.push_back(0);
    }
}

void ExternalMic::Init() {
    MILO_ASSERT(gMics.empty(), 0x3b);
    ExternalMicClientMgr::Init();
    for (unsigned int i = 0; i < 4; i++) {
        gMics.push_back(new ExternalMic(i));
    }
}
