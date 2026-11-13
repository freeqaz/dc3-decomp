#include "utl\OSCMessenger.h"
#include "os\Debug.h"
#include "os\HolmesClient.h"
#include "os\NetworkSocket.h"
#include "os\System.h"
#include "utl\Std.h"

OSCMessenger TheOSCMessenger;

OSCMessenger::~OSCMessenger() {
    delete mSocket1;
    delete mSocket2;
}

void OSCMessenger::Connect() {
    if (!UsingCD()) {
        unsigned int ip = HolmesResolveIP().mIP;
        if ((int)ip != 0) {
            mSocket1 = NetworkSocket::Create(false);
            mSocket1->Bind(0x303A);
            mSocket1->Listen();
            mSocket2 = NetworkSocket::Create(false);
            mSocket2->Connect(ip, 0x3039);
        }
    }
}

void OSCMessenger::Poll() {
    if (mSocket1 && mSocket1->CanRead()) {
        char data[0x80];
        mSocket1->Recv(data, 0x80);
        char str[0x80];
        strncpy(str, data, 0x80);
        str[0x7f] = 0;
        int len = strlen(str);
        int pos = len - len % 4 + 4;
        OSCValue value;
        value.mAddress = str;
        value.mHasNewValue = 1;
        MILO_ASSERT(data[pos] == ',', 0x46);
        char c4 = data[pos + 1];
        // The payload is reached through a running byte cursor, not through
        // constants folded into the array base: the image computes
        // "addi r11, pos, K" and adds the base separately, and in the vector
        // arm it keeps ONE cursor and bumps it by 4 between loads.  Writing
        // &data[pos + K] instead lets MSVC fold K into the base address.
        if (c4 == 's') {
            MILO_ASSERT(data[pos+2] == 0, 0x49);
            MILO_ASSERT(data[pos+3] == 0, 0x4A);
            int payload = pos + 4;
            strncpy(value.buffer, &data[payload], 0x80);
            value.mType = 's';
        } else if (c4 == 'i') {
            MILO_ASSERT(data[pos+2] == 0, 0x51);
            MILO_ASSERT(data[pos+3] == 0, 0x52);
            value.mType = 'i';
            int payload = pos + 4;
            *(int *)value.buffer = *(int *)&data[payload];
        } else if (c4 == 'f' && data[pos + 2] == 'f' && data[pos + 3] == 'f') {
            value.mType = 'v';
            int *valueBuffer = (int *)value.buffer;
            int payload = pos + 8;
            valueBuffer[0] = *(int *)&data[payload];
            payload += 4;
            valueBuffer[1] = *(int *)&data[payload];
            payload += 4;
            valueBuffer[2] = *(int *)&data[payload];
        } else if (c4 == 'f') {
            MILO_ASSERT(data[pos+2] == 0, 0x67);
            MILO_ASSERT(data[pos+3] == 0, 0x68);
            value.mType = 'f';
            int payload = pos + 4;
            *(int *)value.buffer = *(int *)&data[payload];
        }
        bool found = false;
        FOREACH (it, mValues) {
            if (it->mAddress == str) {
                memcpy(it->buffer, value.buffer, 0x80);
                found = true;
                it->mHasNewValue = 1;
                break;
            }
        }
        if (!found) {
            mValues.push_front(value);
        }
    }
}

// The two statements in the value arm are stored flag-first on purpose: c2
// then emits them load-then-store, as the image does, and -- the reason it
// matters -- colours the loaded value onto `intValue`'s own pseudo-register
// (r29) rather than onto a fresh one. With the load written first the value
// lands in a separate register from the parameter, so c2 cannot tail-merge
// the two `return intValue;` sites: the placeholder arm keeps its own copy
// of the exit sequence (three ~String calls, 256 bytes) instead of branching
// back into the value arm's exit. Flag-first keeps a single register, c2
// merges the returns, and the placeholder arm ends in `b` to the value arm's
// exit (target 0x827E7D78..0x827E7D88, `b .L_827E7D78` at 0x827E7DD8).
// Both orders are behaviourally identical -- `mHasNewValue` (0x89) and
// `buffer` (0x8) are disjoint, so neither store can affect the load.
// Measured alternatives, all lower: a single tail `return intValue;` gets the
// image's registers and instruction multiset but lays the merged exit last
// (86.9); `if (!val) {placeholder} else {found}` lays the placeholder arm
// first (34.3 and 66.1 for the two orders); `goto` to a shared label and the
// placeholder-first early-return forms are below 35.
int OSCMessenger::GetInt(String str, int intValue) {
    OSCValue *val = GetValue(str);
    if (val) {
        MILO_ASSERT(val->mType == 'i', 0x131);
        val->mHasNewValue = 0;
        intValue = *(int *)val->buffer;
        return intValue;
    }
    {
        OSCValue newValue;
        newValue.mAddress = str;
        newValue.mHasNewValue = 0;
        newValue.mType = 'i';
        // The image seeds the placeholder's buffer with the caller's default
        // (target 0x827E7DA8: `stw r29, 0x68(r31)`; r31+0x60 is newValue and
        // +0x8 is buffer). We left it uninitialised, so the OSCValue cached for
        // an address nobody had sent yet held stack junk, and any later reader
        // of that same entry saw it.
        *(int *)newValue.buffer = intValue;
        mValues.push_front(newValue);
    }
    return intValue;
}

OSCMessenger::OSCValue *OSCMessenger::GetValue(String str) {
    FOREACH (it, mValues) {
        if (it->mAddress == str) {
            return &(*it);
        }
    }
    return 0;
}

void OSCMessenger::SendOSCFloat(String str, float value) {
    if (mSocket2) {
        char buf[0x100];
        int len = MakeOSCAddress(str, buf);
        int i = len;
        buf[i++] = ',';
        buf[i++] = 'f';
        buf[i++] = '\0';
        buf[i++] = '\0';
        memcpy(&buf[i], &value, sizeof(float));
        mSocket2->Send(buf, i + 4);
    }
}

int OSCMessenger::MakeOSCAddress(String str, char *buf) {
    int len = strlen(str.c_str());
    strncpy(buf, str.c_str(), 0x80);
    int rem = len % 4;
    memset(buf + len, 0, 4 - rem);
    return len - rem + 4;
}

// Lane history (13 lines moved): docs/decomp/notes/system/utl/OSCMessenger.md
float OSCMessenger::GetFloat(String str, float fValue) {
    OSCValue *val = GetValue(str);
    if (val) {
        MILO_ASSERT(val->mType == 'f', 0x149);
        val->mHasNewValue = 0;
        fValue = *(float *)val->buffer;
        return fValue;
    }
    {
        OSCValue newValue;
        newValue.mAddress = str;
        newValue.mHasNewValue = 0;
        newValue.mType = 'f';
        *(float *)newValue.buffer = fValue;
        mValues.push_front(newValue);
    }
    return fValue;
}
