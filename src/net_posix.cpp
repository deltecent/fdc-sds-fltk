// POSIX sockets (Linux, macOS).

#include "net.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

NetSocket netListen(int port, std::string& error)
{
    // A client vanishing mid-write must not kill the process.
    signal(SIGPIPE, SIG_IGN);

    int s = socket(AF_INET6, SOCK_STREAM, 0);
    bool v6 = s >= 0;
    if (!v6)
        s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return NET_INVALID;
    }

    int on = 1, off = 0;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    int rc;
    if (v6) {
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_any;
        addr.sin6_port = htons(static_cast<uint16_t>(port));
        rc = bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    } else {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(static_cast<uint16_t>(port));
        rc = bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    }

    if (rc < 0 || listen(s, 1) < 0) {
        error = "Cannot listen on port " + std::to_string(port) + ": " + std::strerror(errno);
        close(s);
        return NET_INVALID;
    }
    return s;
}

int netWaitReadable(NetSocket s, int timeoutMs)
{
    pollfd p{static_cast<int>(s), POLLIN, 0};
    int r = poll(&p, 1, timeoutMs);
    if (r < 0)
        return errno == EINTR ? 0 : -1;
    return r > 0 ? 1 : 0;
}

NetSocket netAccept(NetSocket listener, std::string& peer)
{
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    int s = accept(static_cast<int>(listener), reinterpret_cast<sockaddr*>(&addr), &len);
    if (s < 0)
        return NET_INVALID;

    int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));

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
    return s;
}

int netRecv(NetSocket s, uint8_t* buf, size_t len)
{
    ssize_t n;
    do {
        n = recv(static_cast<int>(s), buf, len, 0);
    } while (n < 0 && errno == EINTR);
    return n < 0 ? -1 : static_cast<int>(n);
}

bool netSendAll(NetSocket s, const uint8_t* buf, size_t len)
{
    while (len > 0) {
        ssize_t n = send(static_cast<int>(s), buf, len, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        buf += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

void netClose(NetSocket s)
{
    close(static_cast<int>(s));
}
