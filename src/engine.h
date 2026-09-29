// FDC+ serial disk server protocol engine. Runs on its own thread and knows
// nothing about the GUI; the GUI polls view() to update its indicators.
#pragma once

#include "transport.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

constexpr int MAX_DRIVE = 15;
constexpr int NUM_DRIVES = MAX_DRIVE + 1;

struct DriveView {
    bool mounted = false;
    bool enabled = false;       // drive select LED
    bool headLoaded = false;    // head load LED
    bool blockMode = false;     // caller works in blocks rather than tracks
    bool accessed = false;      // track has been reported since mount
    int track = 0;
    int maxTrack = 76;
};

struct EngineView {
    DriveView drives[NUM_DRIVES];
    bool receiving = false;     // data received within the last 3 seconds
    bool running = false;
    std::string status;
};

class Engine {
public:
    Engine() = default;
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // baud is the serial line rate, or 0 for TCP.
    void start(std::unique_ptr<Transport> transport, int baud = 0);
    void stop();

    // Takes ownership of file, which must be open for binary read/write.
    void mount(int drive, std::FILE* file);
    void unmount(int drive);

    EngineView view() const;

private:
    static constexpr size_t CMD_SIZE = 10;
    static constexpr size_t TRACKBUF_LEN = 8192;

    void run();
    void process(const uint8_t* cmd);
    void doRead(uint16_t param1, uint16_t param2);
    void doWrite(uint16_t param1, uint16_t param2);
    void doStat(uint16_t param1, uint16_t param2);
    void sendResponse(const char* name, uint16_t code, uint16_t data);
    bool readExact(uint8_t* buf, size_t len, int timeoutMs);
    void updateIndicators(int drive, int headStatus, int track, int trackLen);

    mutable std::mutex mutex_;          // guards files_ and view_
    std::FILE* files_[NUM_DRIVES] = {};
    EngineView view_;

    std::unique_ptr<Transport> transport_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::chrono::milliseconds gap_{50}; // silence that ends a partial command

    uint8_t trackBuf_[TRACKBUF_LEN + 2] = {};
};
