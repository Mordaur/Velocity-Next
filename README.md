Velocity-Next
=============

**Velocity-Next is a continuation of the original [Velocity](https://github.com/hetelek/Velocity) project, which ceased active development some time ago. This fork aims to modernize the codebase, add new features, and maintain compatibility with current operating systems and toolchains.**

### What's New in Velocity-Next

- **On-demand privileged helper**: raw device access without relaunching the app
  or right-clicking — UAC on Windows, polkit/sudo on Linux
- **Typed device diagnostics**: permission, not-found and busy failures are
  reported instead of a bare "no drives detected"
- **Friendly folder names**: the device browser can show `DOOM II (584109B6)` instead
  of raw IDs, toggled by Ctrl+Click on the Name column (raw mode stays available)
- **Optional XboxUnity lookup**: titles that cannot be identified from the drive
  can be named (and given covers) from the online database — off by default in
  Preferences
- **XEX and SVOD open from the device tree**, not only from packages and ISOs
- **XUIZ, STRB, ZIP and text viewers** (CJK encodings) in the package browser
- **Hardened FATX**: corrupt cluster chains, zero-length files and non-512-byte
  sector sizes are handled safely
- **Smart Pointer Refactoring**: Modern C++20 memory management in XboxInternals
- **MSVC 2022 and MinGW**, CMake 3.20+ presets, standalone XboxInternals library

### Getting Velocity-Next

No packaged release has been published yet. Until the first release is tagged,
build from source using [BUILD.md](BUILD.md). Release archives keep
`velocity-device-helper` next to the executable, which the app needs for raw
device access.

### Credits

Original Velocity was developed mainly by Stevie Hetelekides (Hetelek) and Adam Spindler (Experiment5X), with contributions from [many others](https://github.com/hetelek/Velocity/graphs/contributors). Velocity-Next continues their work with gratitude and respect for the foundation they built.

## Raw Device Access

Velocity always starts unprivileged. Reading a raw disk usually needs
administrator rights, so when the first probe is denied the app starts a small
helper (`velocity-device-helper`, shipped next to the executable) that performs
only the device I/O:

- **Windows** — the helper is launched with a UAC prompt; the app itself keeps
  running as the logged-in user. No right-click or administrator shortcut is
  needed.
- **Linux** — the helper starts through `pkexec`, or through passwordless `sudo`
  when available. Adding your user to the `disk` group also lets the app read
  devices directly.
- **macOS** — there is no elevation helper yet; run the app with `sudo`.

Declining the prompt is not an error: the app reports that administrator access
was not granted and simply leaves the device list empty.

## Running Velocity-Next

1. Keep `velocity-device-helper` in the same folder as the main executable.
2. Start the app normally; it asks for elevation only when raw device access is
   denied, and only for the helper process.
3. If a drive still does not appear, [BUILD.md](BUILD.md#running-velocity)
   documents the manual fallbacks (run as administrator, `disk` group, `sudo`).


About Velocity-Next
-------------------
Velocity-Next is a cross-platform Xbox 360 file management application built with Qt 6. Browse, edit, and extract content from Xbox 360 hard drives, USB devices, game saves, profiles, and disc images.

### Supported File Formats

- **FATX** - Xbox 360 filesystem (hard drives, USB devices)
- **STFS** - Game saves, profiles, DLC packages (CON/LIVE/PIRS)
- **GPD** - Gamer profile data (achievements, avatar awards, settings)
- **XDBF** - Xbox database files
- **ISO** - Disc images (GDF, XGD3, Redump formats) with file extraction
- **XEX** - Xbox executables with metadata parsing
- **SVOD** - Secure Video on Demand packages
- **YTGR** - Avatar asset packages
- **XUIZ** - Dashboard package files, with extraction
- **STRB** - Avatar asset blocks
- **ZIP** - Archives inside ISO images and packages
- **Account** - Profile account data inside STFS packages

### International Text Support

View text files from Japanese, Chinese, and Korean game discs with automatic encoding detection:
- Japanese (Shift-JIS), Simplified Chinese (GBK), Traditional Chinese (Big5), Korean (EUC-KR)
- UTF-8, UTF-16, Latin-1
- Manual encoding override available

Licensing Information
---------------------
Velocity-Next is licensed and distributed under the GNU General Public License (v3), continuing the license of the original Velocity project.

Velocity-Next is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

If no copy of the GNU General Public License was received with this program (FILE: COPYING), it is available at <http://www.gnu.org/licenses/>.

### Third-Party Components

Velocity-Next includes the following third-party libraries:

- **Botan** (BSD-2-Clause): Cryptography library embedded as amalgamation build. See [LICENSE-Botan.txt](LICENSE-Botan.txt) for full license text.
- **Qt 6**: Cross-platform application framework (LGPL v3 / GPL v3). Qt is
  dynamically linked on every platform and shipped alongside the executable.

### Build System Acknowledgments

- Botan CMake integration adapted from [Tectu/botan-cmake](https://github.com/Tectu/botan-cmake)
