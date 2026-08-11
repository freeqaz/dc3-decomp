#pragma once
#include "net_ham\RCJobDingo.h"
#include "obj/Object.h"

class GetMotdJob : public RCJob {
public:
    GetMotdJob(Hmx::Object *callback);
    void GetMotdData(
        unsigned int &challengeInterval,
        int &lastNewSongDt,
        bool &motdXPFlag,
        int &motdFreq,
        std::vector<String> &toasts,
        String &motd,
        String &motdImage,
        String &motdSound,
        String &motdAux,
        String &motdImageAux,
        String &motdSoundAux,
        String &motdMiscImage
    );
};
