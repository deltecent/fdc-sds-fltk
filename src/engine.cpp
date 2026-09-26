// FDC+ serial disk server protocol.
//
// All transactions are initiated by the FDC. Commands and responses are ten
// byte messages: four ASCII command bytes followed by three little endian
// 16 bit words (param1, param2, checksum). The checksum is the 16 bit sum of
// the first eight bytes.
//
// FDC to server:
//   STAT  param1 LSB = selected drive (0xff = none), MSB = head status;
//         param2 = current track. Server answers STAT with a mounted-drive
//         bitmap in the response data word.
//   READ  param1 = drive (upper nibble) and track (lower 12 bits);
//         param2 = transfer length. Server answers with the track data
//         followed by a 16 bit checksum.
//   WRIT  same parameters as READ. Server answers WRIT with OK or NOT READY;
//         if OK the FDC sends the track data plus checksum and the server
//         answers WSTA with the final status.
//
// Response codes: 0 OK, 1 not ready, 2 checksum error, 3 write error.
//
// Commands with a bad checksum are ignored; the FDC retries after one second.

#include "engine.h"

#include <chrono>
#include <cstring>

namespace {

constexpr uint16_t STAT_OK = 0;
constexpr uint16_t STAT_NOT_READY = 1;
constexpr uint16_t STAT_CHECKSUM_ERR = 2;
constexpr uint16_t STAT_WRITE_ERR = 3;

constexpr size_t COMMAND_LENGTH = 8;        // does not include checksum bytes
constexpr int BLOCK_THRESHOLD = 1024;       // track length at or below this means
                                            // the caller is working in blocks
constexpr int MAX_BLOCK_NUM = 399;          // default max block for bargraph scaling
constexpr auto NO_ACTIVITY_TIME = std::chrono::seconds(3);
constexpr int POLL_MS = 10;
constexpr int WRITE_DATA_TIMEOUT_MS = 5000;

using Clock = std::chrono::steady_clock;

uint16_t getWord(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

void putWord(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>(v >> 8);
}

uint16_t checksum(const uint8_t* p, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i)
        sum += p[i];
    return static_cast<uint16_t>(sum);
}

int maxTrackForSize(long size)
{
    if (size < 200000)
        return 34;
    if (size < 500000)
        return 76;
    return 2047;
}

} // namespace

Engine::~Engine()
{
    stop();
    for (int i = 0; i < NUM_DRIVES; ++i)
        unmount(i);
}

void Engine::start(std::unique_ptr<Transport> transport)
{
    stop();
    transport_ = std::move(transport);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        view_.running = true;
        view_.status = transport_->status();
    }
    running_ = true;
    thread_ = std::thread(&Engine::run, this);
}

void Engine::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
    transport_.reset();

    std::lock_guard<std::mutex> lock(mutex_);
    view_.running = false;
    view_.receiving = false;
}

void Engine::mount(int drive, std::FILE* file)
{
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);

    std::lock_guard<std::mutex> lock(mutex_);
    if (files_[drive])
        std::fclose(files_[drive]);
    files_[drive] = file;
    view_.drives[drive] = DriveView();
    view_.drives[drive].mounted = true;
    view_.drives[drive].maxTrack = maxTrackForSize(size);
}

void Engine::unmount(int drive)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (files_[drive])
        std::fclose(files_[drive]);
    files_[drive] = nullptr;
    view_.drives[drive] = DriveView();
}

EngineView Engine::view() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return view_;
}

void Engine::run()
{
    uint8_t cmd[CMD_SIZE];
    size_t idx = 0;
    bool idle = true;
    auto lastRx = Clock::now();

    while (running_) {
        int n = transport_->read(cmd + idx, CMD_SIZE - idx, POLL_MS);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            view_.status = transport_->status();
        }

        if (n < 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            view_.running = false;
            view_.receiving = false;
            return;
        }

        // A gap in the data discards any partial command, which keeps us in
        // sync with the FDC after a garbled or truncated message.
        if (n == 0) {
            idx = 0;
            if (!idle && Clock::now() - lastRx >= NO_ACTIVITY_TIME) {
                idle = true;
                std::lock_guard<std::mutex> lock(mutex_);
                updateIndicators(NUM_DRIVES, 0, 0, 0);
                view_.receiving = false;
            }
            continue;
        }

        lastRx = Clock::now();
        if (idle) {
            idle = false;
            std::lock_guard<std::mutex> lock(mutex_);
            view_.receiving = true;
        }

        idx += static_cast<size_t>(n);
        if (idx < CMD_SIZE)
            continue;
        idx = 0;

        if (checksum(cmd, COMMAND_LENGTH) != getWord(cmd + 8))
            continue;

        process(cmd);
    }
}

void Engine::process(const uint8_t* cmd)
{
    uint16_t param1 = getWord(cmd + 4);
    uint16_t param2 = getWord(cmd + 6);

    if (std::memcmp(cmd, "READ", 4) == 0)
        doRead(param1, param2);
    else if (std::memcmp(cmd, "WRIT", 4) == 0)
        doWrite(param1, param2);
    else if (std::memcmp(cmd, "STAT", 4) == 0)
        doStat(param1, param2);
}

void Engine::doRead(uint16_t param1, uint16_t param2)
{
    int drive = param1 >> 12;
    int track = param1 & 0x0fff;
    size_t trackLen = param2;

    if (trackLen > TRACKBUF_LEN)
        return;                                 // too long, ignore

    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::FILE* f = files_[drive];
        if (!f)
            return;                             // drive not mounted, ignore
        updateIndicators(drive, 0xff, track, static_cast<int>(trackLen));
        if (std::fseek(f, static_cast<long>(track * trackLen), SEEK_SET) != 0)
            return;
        if (std::fread(trackBuf_, 1, trackLen, f) != trackLen)
            return;                             // past end of file, ignore
    }

    putWord(trackBuf_ + trackLen, checksum(trackBuf_, trackLen));
    transport_->write(trackBuf_, trackLen + 2);
}

void Engine::doWrite(uint16_t param1, uint16_t param2)
{
    int drive = param1 >> 12;
    int track = param1 & 0x0fff;
    size_t trackLen = param2;
    uint16_t status = STAT_OK;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!files_[drive])
            status = STAT_NOT_READY;
        if (trackLen > TRACKBUF_LEN)
            status = STAT_NOT_READY;
        else
            updateIndicators(drive, 0xff, track, static_cast<int>(trackLen));
    }

    sendResponse("WRIT", status, 0);
    if (status != STAT_OK)
        return;

    // Read the track data outside the lock; it can take a while at low baud rates.
    if (!readExact(trackBuf_, trackLen + 2, WRITE_DATA_TIMEOUT_MS)
        || checksum(trackBuf_, trackLen) != getWord(trackBuf_ + trackLen)) {
        sendResponse("WSTA", STAT_CHECKSUM_ERR, 0);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::FILE* f = files_[drive];
        if (!f
            || std::fseek(f, static_cast<long>(track * trackLen), SEEK_SET) != 0
            || std::fwrite(trackBuf_, 1, trackLen, f) != trackLen
            || std::fflush(f) != 0)
            status = STAT_WRITE_ERR;
    }

    sendResponse("WSTA", status, 0);
}

void Engine::doStat(uint16_t param1, uint16_t param2)
{
    uint16_t mounted = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        updateIndicators(param1 & 0xff, param1 & 0xff00, param2, BLOCK_THRESHOLD + 1);
        for (int i = 0; i < NUM_DRIVES; ++i)
            if (files_[i])
                mounted |= static_cast<uint16_t>(1u << i);
    }
    sendResponse("STAT", STAT_OK, mounted);
}

void Engine::sendResponse(const char* name, uint16_t code, uint16_t data)
{
    uint8_t rsp[CMD_SIZE];
    std::memcpy(rsp, name, 4);
    putWord(rsp + 4, code);
    putWord(rsp + 6, data);
    putWord(rsp + 8, checksum(rsp, COMMAND_LENGTH));
    transport_->write(rsp, sizeof(rsp));
}

bool Engine::readExact(uint8_t* buf, size_t len, int timeoutMs)
{
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    size_t got = 0;
    while (got < len) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - Clock::now()).count();
        if (remaining <= 0 || !running_)
            return false;
        int n = transport_->read(buf + got, len - got, static_cast<int>(remaining));
        if (n < 0)
            return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

// Set the drive select and head load LEDs and the track number for driveNum.
// A drive number above MAX_DRIVE turns all LEDs off. A track length at or
// below BLOCK_THRESHOLD means the caller is reading blocks rather than tracks,
// so the bargraph is rescaled. Caller must hold mutex_.
void Engine::updateIndicators(int drive, int headStatus, int track, int trackLen)
{
    for (auto& d : view_.drives) {
        d.enabled = false;
        d.headLoaded = false;
    }

    if (drive > MAX_DRIVE || !files_[drive])
        return;

    DriveView& d = view_.drives[drive];
    if (trackLen <= BLOCK_THRESHOLD) {
        if (d.maxTrack < MAX_BLOCK_NUM) {
            d.maxTrack = MAX_BLOCK_NUM;
            d.blockMode = true;
        }
        if (track > d.maxTrack)
            d.maxTrack = track;
    }
    d.track = track;
    d.accessed = true;
    d.enabled = true;
    d.headLoaded = headStatus != 0;
}
