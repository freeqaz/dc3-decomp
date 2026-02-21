#include "synth/MicClientMapper.h"
#include "os/Debug.h"

void MicClientMapper::LockMicID(int micID) {
    for (std::vector<MicMappingData>::iterator it = mMappingData.begin();
         it != mMappingData.end();
         ++it) {
        if (it->mMicID == micID) {
            if (it->bLocked) {
                MILO_NOTIFY(
                    "MicClientMapper::UnlockMicID - Trying to lock an already locked mic!"
                );
            }
            it->bLocked = true;
        }
    }
}

void MicClientMapper::UnlockMicID(int micID) {
    for (std::vector<MicMappingData>::iterator it = mMappingData.begin();
         it != mMappingData.end();
         ++it) {
        if (it->mMicID == micID) {
            if (!it->bLocked) {
                MILO_NOTIFY(
                    "MicClientMapper::UnlockMicID - Trying to unlock an already unlocked mic!"
                );
            }
            it->bLocked = false;
        }
    }
}

bool MicClientMapper::HasMicID(int micID) const {
    for (std::vector<MicMappingData>::const_iterator it = mMappingData.begin();
         it != mMappingData.end();
         ++it) {
        if (it->mMicID == micID)
            return true;
    }
    return false;
}

bool MicClientMapper::IsMicIDLocked(int micID) const {
    for (std::vector<MicMappingData>::const_iterator it = mMappingData.begin();
         it != mMappingData.end();
         ++it) {
        if (it->mMicID == micID) {
            return it->bLocked;
        }
    }
    return false;
}

bool MicClientMapper::GetFirstUnlockedMicID(int &micID) const {
    for (std::vector<MicMappingData>::const_iterator it = mMappingData.begin();
         it != mMappingData.end();
         ++it) {
        if (it->mMicID != -1 && !it->bLocked) {
            micID = it->mMicID;
            return true;
        }
    }
    return false;
}

void MicClientMapper::RefreshPlayerMapping() {
    for (std::vector<PlayerMappingData>::iterator iter = mPlayers.begin();
         iter != mPlayers.end();
         ++iter) {
        if (iter->iPreferredMicID != -1 && HasMicID(iter->iPreferredMicID)) {
            if (iter->iPreferredMicID == iter->iActualMicID) {
                MILO_ASSERT(IsMicIDLocked( iter->iPreferredMicID ), 0x1A2);
            } else {
                if (!IsMicIDLocked(iter->iPreferredMicID)) {
                    if (iter->iActualMicID != -1)
                        UnlockMicID(iter->iActualMicID);
                    iter->iActualMicID = iter->iPreferredMicID;
                    LockMicID(iter->iPreferredMicID);
                }
            }
        }
    }
    for (std::vector<PlayerMappingData>::iterator iter = mPlayers.begin();
         iter != mPlayers.end();
         ++iter) {
        if (iter->iActualMicID != -1 && HasMicID(iter->iActualMicID)) {
            MILO_ASSERT(IsMicIDLocked( iter->iActualMicID ), 0x1C0);
        } else {
            int firstUnlockedID = -1;
            if (GetFirstUnlockedMicID(firstUnlockedID)) {
                iter->iActualMicID = firstUnlockedID;
                LockMicID(firstUnlockedID);
            } else
                iter->iActualMicID = -1;
        }
    }
}

// void MicClientMapper::RefreshMics() {
//     for (std::vector<MicMappingData>::iterator it = mMappingData.begin();
//          it != mMappingData.end();
//          ++it) {
//         if (it->mClientID != -1 && it->mMicID != -1 &&
//         !TheSynth->IsMicConnected(it->mMicID)) {
//             TheSynth->ReleaseMic(it->mMicID);
//             int playerID = GetPlayerIDForMicID(it->mMicID);
//             if (playerID != -1) {
//                 mPlayers[playerID].iActualMicID = -1;
//             }
//             it->mMicID = -1;
//             it->bLocked = false;
//         }
//     }
//     for (std::vector<MicMappingData>::iterator iter = mMappingData.begin();
//          iter != mMappingData.end();
//          ++iter) {
//         if (iter->mClientID != -1 && iter->mMicID == -1) {
//             iter->mMicID = TheSynth->GetNextAvailableMicID();
//             MILO_ASSERT(iter->bLocked == false, 0x186);
//             if (iter->mMicID != -1) {
//                 TheSynth->CaptureMic(iter->mMicID);
//             }
//         }
//     }
//     RefreshPlayerMapping();
// }

void MicClientMapper::HandleMicsChanged() {
    RefreshMics();
    if (mMicManager)
        mMicManager->HandleMicsChanged();
}

int MicClientMapper::GetMicIDForPlayerID(int playerID) const {
    if (playerID >= mNumPlayers)
        return -1;
    else
        return mPlayers[playerID].iActualMicID;
}

int MicClientMapper::GetMicIDForClientID(const MicClientID &clientID) const {
    if (clientID.mPlayerID == -1) {
        for (std::vector<MicMappingData>::const_iterator it = mMappingData.begin();
             it != mMappingData.end();
             ++it) {
            if (it->mClientID == clientID.mClientID)
                return it->mMicID;
        }
        return -1;
    } else
        return GetMicIDForPlayerID(clientID.mPlayerID);
}

MicClientMapper::~MicClientMapper() {}

MicClientMapper::MicClientMapper() : mMicManager(0), mNumPlayers(2) {
    for (int i = 0; i < 4; i++) {
        mMappingData.push_back(MicMappingData());
    }
    for (int i = 0; i < mNumPlayers; i++) {
        mPlayers.push_back(PlayerMappingData());
    }
}
