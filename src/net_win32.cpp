// Winsock.

#include "net.h"

#include <winsock2.h>
#include <ws2tcpip.h>

namespace {

std::string lastError(const char* what)
{
    return std::string(what) + " failed (WSA error " + std::to_string(WSAGetLastError()) + ")";
}

SOCKET sock(NetSocket s)
{
    return static_cast<SOCKET>(s);
}

} // namespace

NetSocket netListen(int port, std::string& error)
{
    static bool started = false;
    if (!started) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            error = "WSAStartup failed";
            return NET_INVALID;
        }
        started = true;
    }

    SOCKET s = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    bool v6 = s != INVALID_SOCKET;
    if (!v6)
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        error = lastError("socket");
        return NET_INVALID;
    }

    BOOL on = TRUE;
    DWORD off = 0;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&on), sizeof(on));

    int rc;
    if (v6) {
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&off), sizeof(off));
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_any;
        addr.sin6_port = htons(static_cast<u_short>(port));
        rc = bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    } else {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(static_cast<u_short>(port));
        rc = bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    }

    if (rc == SOCKET_ERROR || listen(s, 1) == SOCKET_ERROR) {
        error = "Cannot listen on port " + std::to_string(port) + ": " + lastError("bind/listen");
        closesocket(s);
        return NET_INVALID;
    }
    return static_cast<NetSocket>(s);
}

int netWaitReadable(NetSocket s, int timeoutMs)
{
    WSAPOLLFD p{};
    p.fd = sock(s);
    p.events = POLLRDNORM;
    int r = WSAPoll(&p, 1, timeoutMs);
    if (r == SOCKET_ERROR)
        return -1;
    return r > 0 ? 1 : 0;
}

NetSocket netAccept(NetSocket listener, std::string& peer)
{
    sockaddr_storage addr{};
    int len = sizeof(addr);
    SOCKET s = accept(sock(listener), reinterpret_cast<sockaddr*>(&addr), &len);
    if (s == INVALID_SOCKET)
        return NET_INVALID;

    BOOL on = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&on), sizeof(on));

    char host[INET6_ADDRSTRLEN] = "?";
    int port = 0;
    if (addr.ss_family == AF_INET6) {
        auto* a = reinterpret_cast<sockaddr_in6*>(&addr);
        if (IN6_IS_ADDR_V4MAPPED(&a->sin6_addr))
            inet_ntop(AF_INET, &a->sin6_addr.s6_addr[12], host, sizeof(host));
        else
            inet_ntop(AF_INET6, &a->sin6_addr, host, sizeof(host));
        port = ntohs(a->sin6_port);
    } else if (addr.ss_family == AF_INET) {
        auto* a = reinterpret_cast<sockaddr_in*>(&addr);
        inet_ntop(AF_INET, &a->sin_addr, host, sizeof(host));
        port = ntohs(a->sin_port);
    }
    peer = std::string(host) + ":" + std::to_string(port);
    return static_cast<NetSocket>(s);
}

int netRecv(NetSocket s, uint8_t* buf, size_t len)
{
    int n = recv(sock(s), reinterpret_cast<char*>(buf), static_cast<int>(len), 0);
    return n == SOCKET_ERROR ? -1 : n;
}

bool netSendAll(NetSocket s, const uint8_t* buf, size_t len)
{
    while (len > 0) {
        int n = send(sock(s), reinterpret_cast<const char*>(buf), static_cast<int>(len), 0);
        if (n == SOCKET_ERROR)
            return false;
        buf += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

void netClose(NetSocket s)
{
    closesocket(sock(s));
}
