#include "HolmesClient.h"
#include "os/Debug.h"
#include "os/NetStream.h"
#include "os/NetworkSocket.h"
#include "os/Timer.h"
#include "utl/MemMgr.h"
#include "utl/Option.h"
#include <cstdio>
#include <vector>

namespace HolmesClient {
    BinStream *PlatformCreateServerStream(bool quiet, const char *share) {
        std::vector<String> hosts;
        String hostname(HolmesFileHostName());

        if (*hostname.c_str() != '\0') {
            hosts.push_back(hostname);
        }

        while (true) {
            hostname = OptionStr("holmes_host", "");
            if (*hostname.c_str() == '\0') break;
            hosts.push_back(hostname);
        }

        while (true) {
            hostname = OptionStr("xb_host", "");
            if (*hostname.c_str() == '\0') break;
            hosts.push_back(hostname);
        }

        if (!hosts.size()) {
            if (quiet) {
                return 0;
            }
            FormatString fmt(
                "NO HOSTNAME PROVIDED, ADD -CC -holmes_share <share> or -holmes_host <host>"
            );
            TheDebug.Fail(fmt.Str(), 0);
        }

        printf("HolmesClientInit(host={%s", hosts[0].c_str());
        for (unsigned int i = 1; i < hosts.size(); i++) {
            printf(", %s", hosts[i].c_str());
        }
        printf("})\n");

        NetStream *stream = 0;
        int attempt = -1;
        do {
            attempt++;
            if ((unsigned int)attempt >= hosts.size()) {
                if (!quiet) {
                    attempt = 0;
                    Timer::Sleep(1000);
                } else {
                    return 0;
                }
            }

            HolmesSetFileShare(hosts[attempt].c_str(), share);
            NetAddress addr = HolmesResolveIP();

            if (addr.mIP == 0) {
                if (!quiet) {
                    printf("\n\nCOULD NOT RESOLVE HOST ADDRESS '%s'\n\n", HolmesFileHostName());
                }
                continue;
            }

            stream = new NetStream();
            stream->Socket()->SetNoDelay(true);
            stream->ClientConnect(addr);

            if (stream->Fail()) {
                delete stream;
                stream = 0;
                if (!quiet) {
                    printf("\n\nCOULD NOT CONNECT TO HOLMES ADDRESS '%s'\n\n", HolmesFileHostName());
                }
            }
        } while (stream == 0);

        return stream;
    }

    String PlatformGetHostName() {
        return NetworkSocket::GetHostName();
    }

    NetAddress PlatformResolveIP() {
        return HolmesResolveIP();
    }
}
