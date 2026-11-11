# Changelog

## Velocity-Next v0.3.0 - 2026-11-11

### 🔐 Device Access

- **On-demand privileged helper** (`velocity-device-helper`), shipped next to the
  application and started only when a raw-device probe is denied:
  - Windows: named-pipe transport with a DACL for the owning user and a
    medium-integrity label, launched with `ShellExecuteEx(..., "runas")`
  - Linux: Unix socket with peer-credential checks, launched with `pkexec` or
    passwordless `sudo`
  - macOS: `getpeereid()` peer check (elevation still requires `sudo`)
- The GUI no longer needs a right-click "Run as administrator" or a whole-app
  relaunch; declining the prompt reports "Administrator access was not granted"
  on both Windows and Linux
- Typed device diagnostics (permission denied / not found / busy) instead of a
  bare "no drives detected"

### 🐞 Fixes

- FATX: corrupt cluster chains are rejected instead of looping until the process
  dies; zero-length files extract correctly
- DeviceIO: honours the device's logical sector size (4Kn media) and fixes the
  POSIX partial-sector write that wrote the wrong buffer
- Partitions and Cluster Tool dialogs own their data by value, fixing garbage
  values and a SIGSEGV after the dialogs were made non-modal
- GPD editor: `indexToEntry()` returns `std::optional` and no longer hands
  callers an uninitialised entry
- GUI handlers also catch `std::exception`, so container range errors surface as
  a message instead of terminating the app
- ISO: initialised `gdfxHeader` and entry headers; account encryption no longer
  dereferences a null output path
- Disk enumeration probes candidates instead of listing phantom device paths

### 🧩 Formats and UI

- XEX and SVOD files open from the device tree (ported from Gualdimar)
- SVOD packages are tagged as SVOD rather than STFS
- Repository links point at `Mordaur/Velocity-Next`

### 🛠️ Build

- Requires `Qt6::CorePrivate`, fixing builds against Qt 6.10+ (Fedora 44,
  Arch with Qt 6.11/6.12)
- Botan configuration passes an explicit `--prefix`, fixing MinGW builds on
  Windows
- Clean build is warning-free on GCC

## Velocity-Next v0.2.1 - 2025-11-16

### 🔧 Core Refactoring & Improvements

**Memory Management Modernization**
- Integrated ISO/XEX support from [Gualdimar/Velocity](https://github.com/Gualdimar/Velocity)
- Converted XboxInternals library to C++20 smart pointers (std::unique_ptr, std::vector)
- Eliminated manual memory management throughout codebase
- Improved exception safety with RAII patterns
- Fixed STFS package handling and hash table operations

**Benefits**
- Eliminates potential memory leaks through automatic cleanup
- Improved stability and reliability
- Better exception safety for edge cases
- Maintains compatibility with all existing file formats

## Velocity-Next v0.2.0 - 2025-10-30

Complete modernization of the original Velocity project with new build system, Qt 6 migration, and major feature additions.

### 🏗️ Build System & Architecture
- **CMake 3.20+ Superproject**: Modern target-based build system with cross-platform presets
- **MSVC 2022 Support**: Full Visual Studio 2022 compatibility alongside MinGW-w64 13.1.0+
- **Qt 6.7.3+ Migration**: Updated from Qt 5 to modern Qt 6 with signal/slot patterns
- **Botan 3.x Integration**: Cryptography library with amalgamation build (no system dependency)
- **C++20 Standard**: Modern C++ with filesystem library and improved type safety
- **XboxInternals Library**: Standalone redistributable library with include/src separation
- **CMakePresets**: Convenient windows-mingw-debug/release and windows-msvc-debug/release presets
- **Toolchain Files**: Dedicated MSVC toolchain for proper compiler detection

### 🎮 Xbox 360 ISO Support
- **XISO Parser**: Full support for GDF, XGD3, and Redump ISO formats
- **Format Detection**: Automatic magic string detection at multiple known offsets
- **AVL Tree Traversal**: Proper parsing of Xbox 360's binary tree filesystem structure
- **File Extraction**: Single file or batch export with preserved directory structure
- **XEX Metadata**: Extract Media ID, Title ID, version, disc info from default.xex
- **GOD Package Support**: Extract files from Games on Demand packages
- **ISO Browser UI**: Tree view with file details, context menu extraction, search/filter

### 🌏 Text Encoding Module
- **4 Asian Encodings**: CP932 (Shift-JIS), CP936 (GBK), CP950 (Big5), CP949 (EUC-KR)
- **Auto-Detection**: BOM check → UTF-16 alternating pattern → UTF-8 validation → scoring-based legacy encoding
- **Performance Optimization**: O(1) hash map lookups (10,000x faster than linear search)
- **Lazy Initialization**: No startup penalty, thread-safe with QMutex
- **UI Integration**: Encoding dropdown + status bar in text viewer
- **Character Validation**: `supports(QChar)` API for input validation
- **Header Optimization**: 99.8% size reduction (1,383 KB → 3.4 KB) via header/impl split

### 🎨 Dark Mode Support
- **Automatic Theme Detection**: Follows system dark/light mode preference on startup
- **Manual Toggle**: Settings → Appearance menu with instant theme switching
- **Full UI Coverage**: All dialogs, windows, and widgets support both themes
- **Persistent Settings**: Theme preference saved across sessions
- **Platform Integration**: Uses Qt's StyleHints API for system theme detection

### 🔧 Core Features (Inherited & Enhanced)
- **FATX Filesystem**: Read/write Xbox 360 hard drives and USB devices
- **STFS Packages**: Browse and edit game saves, DLC, profiles (CON/LIVE/PIRS)
- **GPD Files**: Edit achievements, avatar awards, gamer profile settings
- **XDBF Database**: Parse and manipulate Xbox database files
- **SVOD Support**: Extract files from Secure Video On Demand packages
- **Avatar Assets**: View and manage avatar clothing and accessories
- **Account Files**: Parse and edit Xbox 360 account data
- **Cryptography**: Full Xbox-specific signature verification and rehashing

###  Bug Fixes & Improvements
- **Type Safety**: Resolved MSVC warnings with explicit static_cast<> in critical paths
- **Path Handling**: Fixed filesystem path conversions for Windows wide-character APIs
- **Memory Management**: Modern RAII patterns throughout codebase
- **Error Handling**: Improved exception safety and error reporting
- **UI Responsiveness**: Non-blocking operations for file extraction and parsing

### 🔮 Upcoming Features (Roadmap)
- **ISO ↔ GOD Conversion** (spec 004): Convert between disc images and Games on Demand packages
- **Enhanced FATX Operations** (spec 006): Copy/move with path preservation in FATX filesystems
- **Plugin System** (specs/PLUGINS.md): Extensible architecture for community contributions

### 📦 Supported Formats
- FATX (Xbox filesystem)
- STFS (game packages, profiles, saves)
- GPD (gamer profile data)
- XDBF (Xbox database files)
- ISO (disc images: GDF, XGD3, Redump)
- XEX (Xbox executables)
- GOD (Games on Demand packages)
- SVOD (Secure Video on Demand)
- YTGR (avatar asset packages)

### 🙏 Credits
Based on the original Velocity by Stevie Hetelekides (Hetelek) and Adam Spindler (Experiment5X).  
Complete modernization and rewrite with CMake, Qt 6, and C++20.
