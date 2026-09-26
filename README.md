# FDC+ Serial Drive Server

Serves Altair disk images to an FDC+ Enhanced Floppy Disk Controller over a
serial port, or to the `fdcplus` device in [altairsim](https://altairsim.com)
over TCP. Runs on Linux, macOS, and Windows as a single self-contained
executable.

Based on the original Windows FDC+ Serial Drive Server by M. Douglas (DeRamp).

Version 1.0. Copyright © 2026 Deltec Enterprises LLC. MIT License.

## Installation

No installer is required. Download the archive for your platform from the
[Releases](https://github.com/deltecent/fdc-sds-fltk/releases) page, extract
it, and put `fdcsds` (`fdcsds.exe` on Windows) wherever you like.

| Platform | Download | Notes |
|---|---|---|
| Windows | [fdcsds-windows-x64.zip](https://github.com/deltecent/fdc-sds-fltk/releases/latest/download/fdcsds-windows-x64.zip) | Windows 10 or later. |
| macOS | [fdcsds-macos-universal.tar.gz](https://github.com/deltecent/fdc-sds-fltk/releases/latest/download/fdcsds-macos-universal.tar.gz) | macOS 11 or later, Apple Silicon and Intel. |
| Linux | [fdcsds-linux-x86_64.tar.gz](https://github.com/deltecent/fdc-sds-fltk/releases/latest/download/fdcsds-linux-x86_64.tar.gz) | Needs a desktop with GTK 3 (nearly all do). |

**macOS:** the program is not signed, so macOS blocks it the first time. Either
right-click it in Finder and choose **Open**, or clear the quarantine flag once:

    xattr -d com.apple.quarantine fdcsds

**Linux:** to use a serial port, your account must be in the `dialout` group
(`uucp` on some distributions). Log out and back in after adding it:

    sudo usermod -aG dialout $USER

## Using the server

### Choosing a connection

The drop-down at the top left selects how the server talks to the Altair.

- **Serial** - pick the serial port and baud rate. The server opens the port
  as soon as you choose either one. **Rescan** refreshes the port list, for
  example after plugging in a USB adapter.
- **TCP** - enter a port number (default 8800) and press Enter. The server
  listens for a connection on that port.

The line at the bottom of the window shows the current connection, such as
`Serial /dev/cu.usbserial-A10K at 403.2K` or
`TCP port 8800: connected to 127.0.0.1:50122`, or an error if the port could
not be opened.

### TCP and altairsim

**TCP mode does not work with a real FDC+.** The FDC+ card only has a serial
connection. TCP mode is for the `fdcplus` device in
[altairsim](https://github.com/deltecent/altairsim), which emulates the FDC+
and can use this server for its serial drive over the network. The byte stream
is exactly the FDC+ serial protocol.

To use it, select **TCP** in the server, load a bootable image in Disk 0, and
connect the simulator's FDC+ to the server with a `socket:HOST:PORT` endpoint.

Start `altairsim` with no arguments or TOML file, which gives its `default`
machine. Run it from a folder without an `altairsim.toml`, because altairsim
loads that file instead if it finds one. Then enter these commands at the
`altairsim>` prompt. For example, with both on the same computer:

```
$ altairsim
altairsim> BOARDS REMOVE dsk0
altairsim> BOARDS ADD fdcplus fdc0
altairsim> SET cpu0 clock_hz=2000000
altairsim> CONNECT fdc0:line socket:localhost:8800
altairsim> RUN FF00
```

Use the server computer's name or IP address instead of `localhost` if it is on
another machine. The simulator must not run at full speed, because CP/M's disk
timeouts are counted in instructions; see the `fdcplus` section of the
altairsim manual for details.

The server accepts one connection at a time. If a new connection arrives, it
replaces the current one, so a restarted simulator can reconnect immediately.

### Serial and altairsim

The simulator's `fdcplus` device can also reach the server over a real serial
line, exactly as an FDC+ would. This is a good way to test a serial setup before
connecting a real Altair. The example below uses a Windows PC with two serial
ports, COM3 (the Serial Drive Server) and COM4 (altairsim), joined by a
null-modem cable. These COM port numbers may be different on your computer.
The server's port list shows the ports it finds, and Windows Device Manager
lists them under **Ports (COM & LPT)**. Wherever the example says COM3 or COM4,
use your own port numbers.

1. In the server, select **Serial**, choose **COM3**, and set the baud rate to
   **230.4K**. Load a bootable image in Disk 0.
2. Start `altairsim` with no arguments or TOML file, which gives its `default`
   machine. Run it from a folder without an `altairsim.toml`, because altairsim
   loads that file instead if it finds one. Then enter these commands at the
   `altairsim>` prompt:

```
C:\> altairsim
altairsim> BOARDS REMOVE dsk0
altairsim> BOARDS ADD fdcplus fdc0
altairsim> SET cpu0 clock_hz=2000000
altairsim> SET fdc0 baud=230400
altairsim> CONNECT fdc0:line serial:COM4
altairsim> RUN FF00
```

The baud rate must be the same at both ends. 230.4K works on most USB serial
adapters; 403.2K and 460.8K are faster but need an adapter that supports them,
and 38.4K is slower but works almost anywhere. As with TCP, the simulator must
not run at full speed; see the `fdcplus` section of the altairsim manual.

On macOS or Linux, use the device names instead, such as
`serial:/dev/cu.usbserial-A10K` or `serial:/dev/ttyUSB1`.

### Baud rate with the FDC+

The FDC+ serial drive runs at 403.2K, 460.8K, or 230.4K baud. These rates
usually need a USB-to-serial adapter rather than a built-in serial port. Always
choose the baud rate in the server; do not set it in the operating system's
port settings.

- **403.2K** is the default and first choice. Try it first even if you don't
  think your adapter supports it.
- **460.8K** is the second choice if 403.2K gives an error or doesn't work.
- **230.4K** works on almost every adapter. Eight inch disk operation is then
  slightly slower than a real drive; the Minidisk still runs at full speed.

If you change the baud rate in the server, make the same change on the FDC+
using its monitor.

On Linux, 403.2K needs a driver that supports arbitrary baud rates. FTDI and
CP210x adapters do; if the server reports that a rate is not supported, try the
next one.

### Baud rate with 88-2SIO serial CP/M

With the 88-2SIOJP, **76.8K** is ideal and gives performance similar to the
original Lifeboat or Burcon CP/M on a real disk drive. Not every USB adapter
supports 76.8K (FTDI adapters do). If it doesn't work, **38.4K** most likely
will. 57.6K, 19.2K, and 9.6K are also available.

### Loading disk images

Each of the four drives, Disk 0 to Disk 3, has its own panel.

- **Load** opens a file chooser. Pick a disk image and the drive is mounted.
  The image must be writable, because the Altair can write to it.
- **Unload** unmounts the drive. Changes are already saved; every write from
  the Altair goes straight to the image file.

The file chooser starts in the folder you last loaded from.

Disk images usually have a `.dsk` extension, and you can recognize them by
size:

| Image | Size |
|---|---|
| Minidisk | 75K |
| Eight inch | 330K |
| 8Mb serial drive | 8768K |

The 8Mb image works only with a serial disk server; it looks like an Altair
eight inch drive with 2048 tracks instead of 77. The server does not care which
type is loaded, but the image type must match the drive type selected on the
FDC+.

Many disk images of original Altair software are linked from the FDC+ page at
[deramp.com](https://deramp.com).

### Indicators

| Indicator | Meaning |
|---|---|
| **Receive** | Green while data is arriving from the Altair. |
| **Disk Enable** | Red when the Altair has selected this drive. |
| **Head Load** | Red when the drive's head is loaded. |
| **Track** | The current track number, also shown on the bargraph. |

When the software reads in blocks rather than whole tracks, as serial CP/M and
serial FLEX do, the label changes to **Block** and the bargraph rescales to
the highest block used.

All lights go out after 3 seconds with no activity.

### Saved settings

The connection type, serial port, baud rate, TCP port, window position, and
last-used folder are saved when the program exits and restored the next time
it starts. Loaded disk images are not remembered.

### Additional notes

The serial disk protocol uses tight timeouts so that disk performance is
realistic. Heavy activity on the same computer, such as video playback, can
occasionally cause disk errors on the Altair while disk I/O is in progress. If
this happens, avoid other work on the computer during disk access.

## Building from source

Requires CMake 3.20+, a C++17 compiler, and git (to fetch FLTK, which is built
and linked statically).

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

The executable is `build/fdcsds` (`build/Release/fdcsds.exe` with Visual Studio).

For a universal macOS binary, add
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`
when configuring.

Linux needs the X11/Wayland development packages FLTK uses, e.g. on Debian/Ubuntu:

    sudo apt install build-essential cmake git libx11-dev libxext-dev libxft-dev \
        libxinerama-dev libxcursor-dev libxrender-dev libxfixes-dev libpango1.0-dev \
        libcairo2-dev libwayland-dev wayland-protocols libxkbcommon-dev \
        libdbus-1-dev libgtk-3-dev

To build X11 only, add `-DFLTK_BACKEND_WAYLAND=OFF`.

GitHub Actions builds all three platforms on every push; pushing a `v*` tag
publishes a release.

### Source layout

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
