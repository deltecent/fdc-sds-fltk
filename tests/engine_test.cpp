// Protocol engine tests. A scripted transport feeds the engine bytes with
// real delays between them, the way a USB serial adapter delivers them, and
// collects what the engine sends back.

#include "../src/engine.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

struct Wire {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<uint8_t> toEngine;
    std::deque<uint8_t> fromEngine;
};

class FakeTransport : public Transport {
public:
    explicit FakeTransport(Wire& wire) : wire_(wire) {}

    int read(uint8_t* buf, size_t len, int timeoutMs) override
    {
        std::unique_lock<std::mutex> lock(wire_.mutex);
        wire_.cv.wait_for(lock, milliseconds(timeoutMs), [&] { return !wire_.toEngine.empty(); });
        size_t n = 0;
        while (n < len && !wire_.toEngine.empty()) {
            buf[n++] = wire_.toEngine.front();
            wire_.toEngine.pop_front();
        }
        return static_cast<int>(n);
    }

    bool write(const uint8_t* buf, size_t len) override
    {
        std::lock_guard<std::mutex> lock(wire_.mutex);
        wire_.fromEngine.insert(wire_.fromEngine.end(), buf, buf + len);
        wire_.cv.notify_all();
        return true;
    }

    std::string status() const override { return "test"; }

private:
    Wire& wire_;
};

uint16_t sum(const uint8_t* p, size_t len)
{
    uint32_t s = 0;
    for (size_t i = 0; i < len; ++i)
        s += p[i];
    return static_cast<uint16_t>(s);
}

std::vector<uint8_t> command(const char* name, uint16_t p1, uint16_t p2)
{
    std::vector<uint8_t> c(10);
    std::memcpy(c.data(), name, 4);
    c[4] = p1 & 0xff; c[5] = p1 >> 8;
    c[6] = p2 & 0xff; c[7] = p2 >> 8;
    uint16_t s = sum(c.data(), 8);
    c[8] = s & 0xff; c[9] = s >> 8;
    return c;
}

void send(Wire& wire, const uint8_t* p, size_t len)
{
    std::lock_guard<std::mutex> lock(wire.mutex);
    wire.toEngine.insert(wire.toEngine.end(), p, p + len);
    wire.cv.notify_all();
}

void send(Wire& wire, const std::vector<uint8_t>& v) { send(wire, v.data(), v.size()); }

void pause(int ms) { std::this_thread::sleep_for(milliseconds(ms)); }

// Wait for a ten byte response. Returns its code, or -1 if none arrived.
int reply(Wire& wire, const char* name, int timeoutMs)
{
    std::unique_lock<std::mutex> lock(wire.mutex);
    if (!wire.cv.wait_for(lock, milliseconds(timeoutMs), [&] { return wire.fromEngine.size() >= 10; }))
        return -1;
    uint8_t r[10];
    for (uint8_t& b : r) {
        b = wire.fromEngine.front();
        wire.fromEngine.pop_front();
    }
    if (std::memcmp(r, name, 4) != 0 || sum(r, 8) != (r[8] | (r[9] << 8)))
        return -1;
    return r[4] | (r[5] << 8);
}

int failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    if (!ok)
        ++failures;
}

// A STAT that arrives in two USB packets with a pause between them. A busy
// machine can sleep far longer than asked, past the 50 ms that ends a partial
// command, so an attempt whose pause overshot doesn't count and is retried.
void splitStat(int baud, int gapMs, const char* what)
{
    auto stat = command("STAT", 0xff, 0);
    for (int attempt = 0; attempt < 10; ++attempt) {
        Wire wire;
        Engine engine;
        engine.start(std::make_unique<FakeTransport>(wire), baud);
        send(wire, stat.data(), 6);
        auto t = Clock::now();
        pause(gapMs);
        send(wire, stat.data() + 6, 4);
        if (Clock::now() - t >= milliseconds(45))
            continue;
        check(reply(wire, "STAT", 500) == 0, what);
        return;
    }
    check(false, what);
    std::printf("      (every pause overshot; this machine is too busy to test it)\n");
}

// Stray bytes run straight into a STAT, with no pause to resynchronize on.
void garbageThenStat()
{
    Wire wire;
    Engine engine;
    engine.start(std::make_unique<FakeTransport>(wire), 230400);
    std::vector<uint8_t> bytes = {0x00, 0x55, 0xaa};
    auto stat = command("STAT", 0xff, 0);
    bytes.insert(bytes.end(), stat.begin(), stat.end());
    send(wire, bytes);
    check(reply(wire, "STAT", 500) == 0, "STAT preceded by stray bytes is answered");
}

// Stray bytes, a long silence, then a STAT.
void garbagePauseStat()
{
    Wire wire;
    Engine engine;
    engine.start(std::make_unique<FakeTransport>(wire), 230400);
    send(wire, {0x00, 0x55, 0xaa});
    pause(300);
    send(wire, command("STAT", 0xff, 0));
    check(reply(wire, "STAT", 500) == 0, "STAT after stray bytes and silence is answered");
}

// Two STATs back to back are both answered.
void backToBack()
{
    Wire wire;
    Engine engine;
    engine.start(std::make_unique<FakeTransport>(wire), 230400);
    auto stat = command("STAT", 0xff, 0);
    std::vector<uint8_t> bytes = stat;
    bytes.insert(bytes.end(), stat.begin(), stat.end());
    send(wire, bytes);
    bool ok = reply(wire, "STAT", 500) == 0 && reply(wire, "STAT", 500) == 0;
    check(ok, "two back to back STATs are both answered");
}

// Write a full 8K track, delivered as slowly as 9600 baud would: about 8.5
// seconds in all, with pauses under a second between pieces.
void slowWrite()
{
    const uint16_t len = 8192;
    std::FILE* f = std::tmpfile();
    std::vector<uint8_t> zeros(len * 2, 0);
    std::fwrite(zeros.data(), 1, zeros.size(), f);

    Wire wire;
    Engine engine;
    engine.mount(0, f);
    engine.start(std::make_unique<FakeTransport>(wire), 9600);

    send(wire, command("WRIT", 0x0001, len));      // drive 0, track 1
    bool ok = reply(wire, "WRIT", 500) == 0;

    std::vector<uint8_t> data(len + 2);
    for (size_t i = 0; i < len; ++i)
        data[i] = static_cast<uint8_t>(i * 7);
    uint16_t s = sum(data.data(), len);
    data[len] = s & 0xff;
    data[len + 1] = s >> 8;

    const size_t chunk = 960;                       // one second at 9600 baud
    for (size_t off = 0; off < data.size(); off += chunk) {
        send(wire, data.data() + off, std::min(chunk, data.size() - off));
        pause(800);
    }
    ok = ok && reply(wire, "WSTA", 2000) == 0;

    std::vector<uint8_t> back(len);
    std::fflush(f);
    std::fseek(f, len, SEEK_SET);
    ok = ok && std::fread(back.data(), 1, len, f) == len
         && std::memcmp(back.data(), data.data(), len) == 0;
    check(ok, "8K track written at 9600 baud pace");
}

// A write whose data stops part way gives up about a second after the last
// byte, rather than waiting out a fixed long timeout.
void stalledWrite()
{
    const uint16_t len = 4096;
    std::FILE* f = std::tmpfile();
    std::vector<uint8_t> zeros(len * 2, 0);
    std::fwrite(zeros.data(), 1, zeros.size(), f);

    Wire wire;
    Engine engine;
    engine.mount(0, f);
    engine.start(std::make_unique<FakeTransport>(wire), 230400);

    send(wire, command("WRIT", 0x0000, len));
    bool ok = reply(wire, "WRIT", 500) == 0;
    std::vector<uint8_t> half(len / 2, 0x42);
    send(wire, half);
    ok = ok && reply(wire, "WSTA", 2500) == 2;     // checksum error
    check(ok, "stalled write fails about a second after the last byte");

    // The engine is listening for commands again.
    send(wire, command("STAT", 0xff, 0));
    check(reply(wire, "STAT", 500) == 0, "STAT answered after a stalled write");
}

} // namespace

int main()
{
    splitStat(230400, 15, "STAT split by a 15 ms USB gap is answered (230.4K)");
    splitStat(9600, 30, "STAT split by a 30 ms USB gap is answered (9.6K)");
    garbageThenStat();
    garbagePauseStat();
    backToBack();
    stalledWrite();
    slowWrite();

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
