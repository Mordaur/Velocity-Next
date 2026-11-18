Velocity-Next
=============

**Velocity-Next is a continuation of the original [Velocity](https://github.com/hetelek/Velocity) project, which [ceased active development](https://github.com/hetelek/Velocity/commit/928af4ced6ff4dde990cca960e110af6476139fd) some time ago. This fork aims to modernize the codebase, add new features, and maintain compatibility with current operating systems and toolchains.**

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
- **Experimental macOS elevation**: the device helper is started through macOS's
  administrator prompt instead of requiring the whole app to run under `sudo`
  (unverified on real hardware — please report)
- **XEX and SVOD open from the device tree**, not only from packages and ISOs
- **XUIZ, STRB, ZIP and text viewers** (CJK encodings) in the package browser
- **Hardened FATX**: corrupt cluster chains, zero-length files and non-512-byte
  sector sizes are handled safely
- **Smart Pointer Refactoring**: Modern C++20 memory management in XboxInternals
- **MinGW (Windows), GCC (Linux), Clang (macOS)**, CMake 3.25+ presets, standalone XboxInternals library

### Backend findings

The format and kernel research behind `XboxInternals` is documented in the
[project wiki](https://github.com/Mordaur/Velocity-Next/wiki). It covers
FATX/XTAF, STFS, content and profile layout, device access and hardening, with
a [coverage matrix](https://github.com/Mordaur/Velocity-Next/wiki/Coverage) that
maps every behavioural claim to its source of truth and verification. The
source tree is the implementation; the wiki is the evidence behind it.

### Getting Velocity-Next

Pre-built archives are published on the
[Releases page](https://github.com/Mordaur/Velocity-Next/releases); **v0.3.2** is
the latest. Every archive keeps `velocity-device-helper` next to the executable,
which the app needs for raw device access.

| Platform | Archive | Notes |
|---|---|---|
| Windows | `Velocity-Next-Windows.zip` | Qt bundled |
| macOS | `Velocity-Next.dmg` | arm64, unsigned |
| Linux | `Velocity-Next-Linux.tar.gz` | requires Qt 6.10.3 on the host |

To build from source instead, see [BUILD.md](BUILD.md).

### Credits

Original Velocity was developed mainly by Stevie Hetelekides (Hetelek) and Adam Spindler (Experiment5X), with contributions from [many others](https://github.com/hetelek/Velocity/graphs/contributors). Velocity-Next continues their work with gratitude and respect for the foundation they built.

### Adapted mechanisms

**Device-browser name resolution — adapted from GODspeed.** The friendly-name
layer is an independent re-implementation of the mechanism used by
[GODspeed](https://github.com/Nemiroff/godspeed) (Nemiroff) in
[`TitleRecognizer`](https://github.com/Nemiroff/godspeed/blob/master/Neurotoxin.Godspeed/Neurotoxin.Godspeed.Shell/ContentProviders/TitleRecognizer.cs):

1. recognise system and content-type folders locally from their names,
2. resolve a game's name from its own package metadata (`titleName`), including
   the shared `/Content/0000000000000000` copy,
3. fall back to an opt-in [XboxUnity](http://xboxunity.net) lookup, cached on disk.

GODspeed publishes no licence and no source code from it is included, linked or
copied here. The behaviour, recognition order and title-ID rules were re-derived
from that implementation and written from scratch for our C++20/Qt stack.

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
- **macOS** — **experimental and untested**: the helper is started through
  `osascript … with administrator privileges`, so macOS shows its usual
  administrator prompt. If that does not work on your machine, run the app with
  `sudo` instead and please report it.

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
- **ISO** - Disc images with file extraction; XGD2/XGD3 are tested on retail
  images, while XGD1 and 2352-byte raw dumps are **experimental**
- **XEX** - Xbox executables with metadata and data extraction; raw/zero-based
  and XCompress/LZX (type 2) are supported, delta (type 3) is not
- **SVOD/GOD** - Secure Video on Demand / Games on Demand packages
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
  dynamically linked: the Windows zip and macOS bundle ship it, while the Linux
  tarball expects Qt 6.10.3 on the host.

### Build System Acknowledgments

- Botan CMake integration adapted from [Tectu/botan-cmake](https://github.com/Tectu/botan-cmake)
