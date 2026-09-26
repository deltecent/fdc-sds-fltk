// Minimal socket layer (implemented per platform in net_*.cpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

using NetSocket = std::intptr_t;
constexpr NetSocket NET_INVALID = -1;

// Open a TCP socket listening on all interfaces.
NetSocket netListen(int port, std::string& error);

// Wait until the socket is readable (or has a pending connection).
// Returns 1 if ready, 0 on timeout, -1 on error.
int netWaitReadable(NetSocket s, int timeoutMs);

// Accept a pending connection; peer receives the remote address.
NetSocket netAccept(NetSocket listener, std::string& peer);

// Returns bytes received, 0 if the peer closed, -1 on error.
int netRecv(NetSocket s, uint8_t* buf, size_t len);

bool netSendAll(NetSocket s, const uint8_t* buf, size_t len);

void netClose(NetSocket s);
