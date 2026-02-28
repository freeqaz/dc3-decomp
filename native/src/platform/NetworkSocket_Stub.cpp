// DC3 Native Port - NetworkSocket Stub
// Replaces NetworkSocket_Win.cpp

#include "os/NetworkSocket.h"
#include "utl/Str.h"

// Stub socket that does nothing
class NativeNetworkSocket : public NetworkSocket {
public:
    virtual ~NativeNetworkSocket() {}
    virtual bool Connect(unsigned int, unsigned short) { return false; }
    virtual bool Fail() const { return true; }
    virtual void Disconnect() {}
    virtual void Bind(unsigned short) {}
    virtual int InqBoundPort(unsigned short &) const { return -1; }
    virtual void Listen() {}
    virtual NetworkSocket *Accept() { return nullptr; }
    virtual void GetRemoteIP(unsigned int &ip, unsigned short &port) { ip = 0; port = 0; }
    virtual bool CanSend() const { return false; }
    virtual bool CanRead() const { return false; }
    virtual int Send(const void *, unsigned int) { return -1; }
    virtual int Recv(void *, unsigned int) { return -1; }
    virtual int SendTo(const void *, unsigned int, unsigned int, unsigned short) { return -1; }
    virtual int BroadcastTo(const void *, unsigned int, unsigned short) { return -1; }
    virtual int RecvFrom(void *, unsigned int, unsigned int &, unsigned short &) { return -1; }
    virtual bool SetNoDelay(bool) { return false; }
};

NetworkSocket *NetworkSocket::Create(bool) {
    return new NativeNetworkSocket();
}

String NetworkSocket::GetHostName() {
    return String("localhost");
}
