# FDC+ Serial Drive Server

Serves Altair disk images to an FDC+ Enhanced Floppy Disk Controller over a
serial port or a TCP connection. Cross-platform port of the original Delphi
program by M. Douglas (DeRamp).

Version 1.0. Copyright © 2026 Deltec Enterprises LLC.

Builds as a single self-contained executable on Linux, macOS, and Windows.
FLTK is fetched and linked statically at configure time.

## Building

Requires CMake 3.20+, a C++17 compiler, and git (to fetch FLTK).

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

The executable is `build/fdcsds` (`build/Release/fdcsds.exe` with Visual Studio).

Linux needs the X11/Wayland development packages FLTK uses, e.g. on Debian/Ubuntu:

    sudo apt install build-essential cmake git libx11-dev libxext-dev libxft-dev \
        libxinerama-dev libxcursor-dev libxrender-dev libxfixes-dev libpango1.0-dev \
        libwayland-dev wayland-protocols libxkbcommon-dev libdbus-1-dev

To build X11 only, add `-DFLTK_BACKEND_WAYLAND=OFF`.

## Connections

- **Serial** - 8N1, no flow control, DTR/RTS off. 403.2K is preferred; 460.8K
  and 230.4K for the FDC+, and slower rates for 2SIO serial CP/M.
  On Linux, add yourself to the `dialout` group to access serial ports.
- **TCP** - the server listens on the chosen port (default 8800) and serves
  one client at a time. The byte stream is exactly the serial protocol.
  A new connection replaces the current one.

## Source layout

| File | Purpose |
|---|---|
| `src/main.cpp` | FLTK user interface |
| `src/engine.*` | Protocol engine (runs on its own thread) |
| `src/transport.h` | Transport interface |
| `src/tcp_server.cpp` | TCP transport, on top of `net.h` |
| `src/serial_posix.cpp` | Serial transport for Linux and macOS |
| `src/serial_linux.cpp` / `serial_macos.cpp` | Custom baud rates, port enumeration |
| `src/serial_win32.cpp` | Serial transport for Windows |
| `src/net_posix.cpp` / `net_win32.cpp` | Sockets |

The sources contain no conditional compilation; `CMakeLists.txt` picks the
platform files.

Settings (connection, baud rate, TCP port, window position) are stored with
`Fl_Preferences` in the user's configuration directory.
