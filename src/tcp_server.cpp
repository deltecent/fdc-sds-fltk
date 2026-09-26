// TCP transport: the server listens on a port and serves one client at a
// time. A new connection replaces the current one, so a client that restarts
// without closing its socket cleanly can reconnect immediately.

#include "net.h"
#include "transport.h"

namespace {

class TcpServer : public Transport {
public:
    TcpServer(NetSocket listener, int port) : listener_(listener), port_(port) {}

    ~TcpServer() override
    {
        dropClient();
        netClose(listener_);
    }

    int read(uint8_t* buf, size_t len, int timeoutMs) override
    {
        if (netWaitReadable(listener_, client_ == NET_INVALID ? timeoutMs : 0) > 0) {
            std::string peer;
            NetSocket s = netAccept(listener_, peer);
            if (s != NET_INVALID) {
                dropClient();
                client_ = s;
                peer_ = peer;
            }
        }

        if (client_ == NET_INVALID)
            return 0;

        int r = netWaitReadable(client_, timeoutMs);
        if (r == 0)
            return 0;
        int n = r > 0 ? netRecv(client_, buf, len) : -1;
        if (n <= 0) {
            dropClient();               // closed or failed; wait for a new client
            return 0;
        }
        return n;
    }

    bool write(const uint8_t* buf, size_t len) override
    {
        if (client_ == NET_INVALID)
            return false;
        if (!netSendAll(client_, buf, len)) {
            dropClient();
            return false;
        }
        return true;
    }

    std::string status() const override
    {
        std::string s = "TCP port " + std::to_string(port_);
        if (client_ == NET_INVALID)
            return s + ": waiting for connection";
        return s + ": connected to " + peer_;
    }

private:
    void dropClient()
    {
        if (client_ != NET_INVALID)
            netClose(client_);
        client_ = NET_INVALID;
        peer_.clear();
    }

    NetSocket listener_;
    NetSocket client_ = NET_INVALID;
    int port_;
    std::string peer_;
};

} // namespace

std::unique_ptr<Transport> openTcpServer(int port, std::string& error)
{
    if (port < 1 || port > 65535) {
        error = "Invalid TCP port " + std::to_string(port);
        return nullptr;
    }
    NetSocket s = netListen(port, error);
    if (s == NET_INVALID)
        return nullptr;
    return std::make_unique<TcpServer>(s, port);
}
