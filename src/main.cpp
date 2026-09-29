// FDC+ Serial Drive Server - serves Altair disk images to an FDC+ Enhanced
// Floppy Disk Controller over a serial port or a TCP connection.

#include "engine.h"
#include "transport.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Preferences.H>
#include <FL/Fl_Progress.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/fl_utf8.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr int UI_DRIVES = 4;
constexpr int WIN_W = 512;
constexpr int PANEL_H = 104;
constexpr int PANEL_TOP = 76;
constexpr int DEFAULT_TCP_PORT = 8800;
constexpr double REFRESH_SECONDS = 0.05;

// Approximation of the MITS Altair 8800 case blue.
const Fl_Color ALTAIR_BLUE = fl_rgb_color(0x4A, 0x7A, 0xB0);
const Fl_Color ALTAIR_BLUE_DARK = fl_rgb_color(0x35, 0x5A, 0x85);
const Fl_Color LED_OFF = fl_rgb_color(128, 128, 128);
const Fl_Color LED_RED = FL_RED;
const Fl_Color LED_GREEN = fl_rgb_color(0, 255, 0);

struct BaudRate {
    int rate;
    const char* label;
};

const BaudRate BAUD_RATES[] = {
    {403200, "403.2K"}, {460800, "460.8K"}, {230400, "230.4K"}, {76800, "76.8K"},
    {57600, "57.6K"},   {38400, "38.4K"},   {19200, "19.2K"},   {9600, "9.6K"},
};
constexpr int NUM_BAUD_RATES = sizeof(BAUD_RATES) / sizeof(BAUD_RATES[0]);

enum Mode { MODE_SERIAL = 0, MODE_TCP = 1 };

// Round indicator lamp.
class Led : public Fl_Widget {
public:
    Led(int x, int y, int d) : Fl_Widget(x, y, d, d) {}

    void set(Fl_Color c)
    {
        if (c != color()) {
            color(c);
            redraw();
        }
    }

protected:
    void draw() override
    {
        fl_color(color());
        fl_pie(x(), y(), w(), h(), 0, 360);
        fl_color(FL_DARK3);
        fl_arc(x(), y(), w(), h(), 0, 360);
    }
};

Fl_Box* whiteLabel(int x, int y, int w, int h, const char* text, int size = 12)
{
    auto* b = new Fl_Box(x, y, w, h, text);
    b->labelcolor(FL_WHITE);
    b->labelsize(size);
    b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    return b;
}

// Escape characters Fl_Menu_::add() treats specially (e.g. '/' makes submenus).
std::string menuEscape(const std::string& s)
{
    std::string out;
    for (char c : s) {
        if (c == '/' || c == '\\' || c == '&' || c == '_')
            out += '\\';
        out += c;
    }
    return out;
}

// Shorten a path by removing characters from its middle so it fits width pixels.
std::string fitPath(const std::string& path, int width, Fl_Font font, int size)
{
    fl_font(font, size);
    if (fl_width(path.c_str()) <= width)
        return path;

    for (size_t keep = path.size() - 1; keep > 4; --keep) {
        size_t head = keep / 2;
        size_t tail = path.size() - (keep - head);
        while (head > 0 && (path[head] & 0xC0) == 0x80)
            --head;
        while (tail < path.size() && (path[tail] & 0xC0) == 0x80)
            ++tail;
        std::string s = path.substr(0, head) + "..." + path.substr(tail);
        if (fl_width(s.c_str()) <= width)
            return s;
    }
    return path;
}

struct DrivePanel {
    Fl_Output* file = nullptr;
    Fl_Button* load = nullptr;
    Fl_Button* unload = nullptr;
    Led* enable = nullptr;
    Led* head = nullptr;
    Fl_Box* trackLabel = nullptr;
    Fl_Box* trackValue = nullptr;
    Fl_Progress* graph = nullptr;
    std::string path;
    DriveView shown;
};

class App {
public:
    App();
    int run(int argc, char** argv);

private:
    void buildWindow();
    void buildDrivePanel(int drive, int y);
    void loadPreferences();
    void savePreferences();

    void scanSerialPorts();
    void updateModeWidgets();
    void connect(bool reportErrors);
    void loadDisk(int drive);
    void unloadDisk(int drive);
    void refresh();
    void close();

    static void onConnectionChange(Fl_Widget*, void* app) { static_cast<App*>(app)->connect(true); }
    static void onRescan(Fl_Widget*, void* app);
    static void onLoad(Fl_Widget* w, void* app);
    static void onUnload(Fl_Widget* w, void* app);
    static void onAbout(Fl_Widget*, void*);
    static void onClose(Fl_Widget*, void* app) { static_cast<App*>(app)->close(); }
    static void onTimer(void* app);

    Engine engine_;
    Fl_Preferences prefs_;

    Fl_Double_Window* window_ = nullptr;
    Fl_Choice* mode_ = nullptr;
    Fl_Choice* serialPort_ = nullptr;
    Fl_Choice* baud_ = nullptr;
    Fl_Button* rescan_ = nullptr;
    Fl_Int_Input* tcpPort_ = nullptr;
    Led* rxLed_ = nullptr;
    Fl_Box* status_ = nullptr;
    DrivePanel drives_[UI_DRIVES];

    std::vector<std::string> portNames_;
    std::string serialName_;
    std::string lastDir_;
    std::string errorStatus_;
    int winX_ = -1, winY_ = -1;
};

App::App() : prefs_(Fl_Preferences::USER_L, "deltecent.com", "fdcsds") {}

int App::run(int argc, char** argv)
{
    Fl::scheme("gtk+");
    buildWindow();
    loadPreferences();
    scanSerialPorts();
    updateModeWidgets();

    if (winX_ >= 0 && winY_ >= 0) {
        int sx, sy, sw, sh;
        Fl::screen_work_area(sx, sy, sw, sh, winX_, winY_);
        if (winX_ >= sx && winY_ >= sy && winX_ < sx + sw - 50 && winY_ < sy + sh - 50)
            window_->position(winX_, winY_);
    }
    window_->show(argc, argv);

    connect(false);
    Fl::add_timeout(REFRESH_SECONDS, onTimer, this);
    return Fl::run();
}

void App::buildWindow()
{
    int winH = PANEL_TOP + UI_DRIVES * (PANEL_H + 8) + 28;
    window_ = new Fl_Double_Window(WIN_W, winH, "FDC+ Serial Drive Server");
    window_->color(ALTAIR_BLUE);
    window_->callback(onClose, this);

    static const std::string title = std::string("FDC+ Serial Drive Server v") + FDCSDS_VERSION;
    whiteLabel(10, 6, 300, 20, title.c_str(), 14)->labelfont(FL_HELVETICA_BOLD);
    auto* about = new Fl_Button(WIN_W - 170, 5, 64, 22, "About");
    about->callback(onAbout, this);

    whiteLabel(WIN_W - 90, 6, 60, 20, "Receive");
    rxLed_ = new Led(WIN_W - 28, 9, 14);
    rxLed_->color(LED_OFF);

    mode_ = new Fl_Choice(10, 38, 90, 24);
    mode_->add("Serial");
    mode_->add("TCP");
    mode_->value(MODE_SERIAL);
    mode_->callback(onConnectionChange, this);

    serialPort_ = new Fl_Choice(108, 38, 210, 24);
    serialPort_->callback(onConnectionChange, this);

    baud_ = new Fl_Choice(324, 38, 90, 24);
    for (const auto& b : BAUD_RATES)
        baud_->add(b.label);
    baud_->value(0);
    baud_->callback(onConnectionChange, this);

    rescan_ = new Fl_Button(420, 38, 82, 24, "Rescan");
    rescan_->callback(onRescan, this);

    tcpPort_ = new Fl_Int_Input(200, 38, 80, 24, "Listen on port");
    tcpPort_->labelcolor(FL_WHITE);
    tcpPort_->labelsize(12);
    tcpPort_->when(FL_WHEN_ENTER_KEY | FL_WHEN_RELEASE);
    tcpPort_->callback(onConnectionChange, this);

    for (int i = 0; i < UI_DRIVES; ++i)
        buildDrivePanel(i, PANEL_TOP + i * (PANEL_H + 8));

    status_ = whiteLabel(10, winH - 24, WIN_W - 200, 20, "");
    whiteLabel(WIN_W - 190, winH - 24, 185, 20, "\xC2\xA9" "2026 Deltec Enterprises LLC")->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);

    window_->end();
}

void App::buildDrivePanel(int drive, int y)
{
    DrivePanel& p = drives_[drive];

    auto* frame = new Fl_Box(FL_ENGRAVED_FRAME, 6, y, WIN_W - 12, PANEL_H, nullptr);
    frame->color(ALTAIR_BLUE);

    auto* title = whiteLabel(16, y + 6, 100, 20, nullptr, 14);
    title->copy_label(("Disk " + std::to_string(drive)).c_str());
    title->labelfont(FL_HELVETICA_BOLD);

    p.file = new Fl_Output(16, y + 30, 366, 24);
    p.file->textsize(12);

    p.load = new Fl_Button(388, y + 30, 52, 24, "Load");
    p.load->callback(onLoad, this);

    p.unload = new Fl_Button(444, y + 30, 56, 24, "Unload");
    p.unload->callback(onUnload, this);
    p.unload->deactivate();

    int row = y + 68;
    whiteLabel(16, row, 70, 20, "Disk Enable");
    p.enable = new Led(88, row + 3, 14);
    p.enable->color(LED_OFF);

    whiteLabel(122, row, 70, 20, "Head Load");
    p.head = new Led(188, row + 3, 14);
    p.head->color(LED_OFF);

    p.trackLabel = whiteLabel(224, row, 42, 20, "Track");
    p.trackValue = whiteLabel(266, row, 40, 20, "");
    p.trackValue->labelfont(FL_HELVETICA_BOLD);

    p.graph = new Fl_Progress(310, row + 4, 190, 12);
    p.graph->box(FL_THIN_DOWN_BOX);
    p.graph->color(ALTAIR_BLUE_DARK);
    p.graph->selection_color(LED_GREEN);
    p.graph->minimum(0);
    p.graph->maximum(76);
    p.graph->value(0);
}

void App::loadPreferences()
{
    char buf[1024];
    int value;

    prefs_.get("SerialPort", buf, "", sizeof(buf));
    serialName_ = buf;
    prefs_.get("LastDir", buf, "", sizeof(buf));
    lastDir_ = buf;

    prefs_.get("Mode", value, MODE_SERIAL);
    mode_->value(value == MODE_TCP ? MODE_TCP : MODE_SERIAL);

    prefs_.get("BaudIndex", value, 0);
    baud_->value(value >= 0 && value < NUM_BAUD_RATES ? value : 0);

    prefs_.get("TcpPort", value, DEFAULT_TCP_PORT);
    tcpPort_->value(std::to_string(value).c_str());

    prefs_.get("WindowX", winX_, -1);
    prefs_.get("WindowY", winY_, -1);
}

void App::savePreferences()
{
    prefs_.set("Mode", mode_->value());
    if (!serialName_.empty())
        prefs_.set("SerialPort", serialName_.c_str());
    prefs_.set("BaudIndex", baud_->value());
    prefs_.set("TcpPort", std::atoi(tcpPort_->value()));
    prefs_.set("LastDir", lastDir_.c_str());
    prefs_.set("WindowX", window_->x());
    prefs_.set("WindowY", window_->y());
    prefs_.flush();
}

void App::scanSerialPorts()
{
    portNames_ = listSerialPorts();
    serialPort_->clear();
    for (const auto& name : portNames_)
        serialPort_->add(menuEscape(name).c_str());

    serialPort_->value(-1);
    for (size_t i = 0; i < portNames_.size(); ++i)
        if (portNames_[i] == serialName_)
            serialPort_->value(static_cast<int>(i));
}

void App::updateModeWidgets()
{
    bool serial = mode_->value() == MODE_SERIAL;
    for (Fl_Widget* w : {static_cast<Fl_Widget*>(serialPort_), static_cast<Fl_Widget*>(baud_),
                         static_cast<Fl_Widget*>(rescan_)}) {
        if (serial)
            w->show();
        else
            w->hide();
    }
    if (serial)
        tcpPort_->hide();
    else
        tcpPort_->show();
}

void App::connect(bool reportErrors)
{
    engine_.stop();
    updateModeWidgets();

    std::string error;
    std::unique_ptr<Transport> transport;
    int baud = 0;

    if (mode_->value() == MODE_SERIAL) {
        int idx = serialPort_->value();
        if (idx < 0 || idx >= static_cast<int>(portNames_.size())) {
            errorStatus_ = portNames_.empty() ? "No serial ports found" : "Select a serial port";
            return;
        }
        serialName_ = portNames_[idx];
        const BaudRate& b = BAUD_RATES[baud_->value() < 0 ? 0 : baud_->value()];
        transport = openSerial(serialName_, b.rate, b.label, error);
        baud = b.rate;
    } else {
        transport = openTcpServer(std::atoi(tcpPort_->value()), error);
    }

    if (!transport) {
        errorStatus_ = error;
        if (reportErrors)
            fl_alert("%s", error.c_str());
        return;
    }

    errorStatus_.clear();
    engine_.start(std::move(transport), baud);
}

void App::onAbout(Fl_Widget*, void*)
{
    fl_message_title("About FDC+ Serial Drive Server");
    fl_message("FDC+ Serial Drive Server\n"
               "Version %s\n"
               "\xC2\xA9 2026 Deltec Enterprises LLC\n"
               "\n"
               "Based on the original FDC+ Serial Drive Server\n"
               "by M. Douglas, DeRamp (deramp.com).",
               FDCSDS_VERSION);
}

void App::onRescan(Fl_Widget*, void* app)
{
    auto* self = static_cast<App*>(app);
    self->scanSerialPorts();
    self->connect(false);
}

void App::onLoad(Fl_Widget* w, void* app)
{
    auto* self = static_cast<App*>(app);
    for (int i = 0; i < UI_DRIVES; ++i)
        if (w == self->drives_[i].load)
            self->loadDisk(i);
}

void App::onUnload(Fl_Widget* w, void* app)
{
    auto* self = static_cast<App*>(app);
    for (int i = 0; i < UI_DRIVES; ++i)
        if (w == self->drives_[i].unload)
            self->unloadDisk(i);
}

void App::loadDisk(int drive)
{
    Fl_Native_File_Chooser chooser;
    chooser.title(("Load Disk " + std::to_string(drive)).c_str());
    chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
    chooser.filter("Disk Image Files\t*.dsk\nAll Files\t*");
    if (!lastDir_.empty())
        chooser.directory(lastDir_.c_str());
    if (chooser.show() != 0)
        return;

    std::string path = chooser.filename();
    std::FILE* f = fl_fopen(path.c_str(), "r+b");
    if (!f) {
        fl_alert("Cannot open %s for read/write.", path.c_str());
        return;
    }

    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos)
        lastDir_ = path.substr(0, slash);

    engine_.mount(drive, f);

    DrivePanel& p = drives_[drive];
    p.path = path;
    p.file->value(fitPath(path, p.file->w() - 8, p.file->textfont(), p.file->textsize()).c_str());
    p.file->tooltip(p.path.c_str());
    p.load->deactivate();
    p.unload->activate();
}

void App::unloadDisk(int drive)
{
    engine_.unmount(drive);

    DrivePanel& p = drives_[drive];
    p.path.clear();
    p.file->value("");
    p.file->tooltip(nullptr);
    p.load->activate();
    p.unload->deactivate();
}

// Copy the engine's state onto the widgets, touching only what changed.
void App::refresh()
{
    EngineView v = engine_.view();

    rxLed_->set(v.receiving ? LED_GREEN : LED_OFF);

    const std::string& status = v.running || errorStatus_.empty() ? v.status : errorStatus_;
    if (!status_->label() || status != status_->label())
        status_->copy_label(status.c_str());

    for (int i = 0; i < UI_DRIVES; ++i) {
        DrivePanel& p = drives_[i];
        const DriveView& d = v.drives[i];

        p.enable->set(d.enabled ? LED_RED : LED_OFF);
        p.head->set(d.headLoaded ? LED_RED : LED_OFF);

        if (d.blockMode != p.shown.blockMode)
            p.trackLabel->label(d.blockMode ? "Block" : "Track");

        if (d.maxTrack != p.shown.maxTrack)
            p.graph->maximum(static_cast<float>(d.maxTrack));

        if (d.track != p.shown.track || d.accessed != p.shown.accessed) {
            p.trackValue->copy_label(d.accessed ? std::to_string(d.track).c_str() : "");
            p.graph->value(static_cast<float>(d.track));
        }

        p.shown = d;
    }
}

void App::onTimer(void* app)
{
    static_cast<App*>(app)->refresh();
    Fl::repeat_timeout(REFRESH_SECONDS, onTimer, app);
}

void App::close()
{
    // Escape closes FLTK windows by default; only close on a real close request.
    if (Fl::event() == FL_SHORTCUT && Fl::event_key() == FL_Escape)
        return;

    savePreferences();
    engine_.stop();
    for (int i = 0; i < NUM_DRIVES; ++i)
        engine_.unmount(i);
    window_->hide();
}

} // namespace

int main(int argc, char** argv)
{
    App app;
    return app.run(argc, argv);
}
