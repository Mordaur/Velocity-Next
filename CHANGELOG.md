# Changelog

## Unreleased

### 🧱 Build and IO

- `DeviceIO::Length()` now falls back to the file size when the block-geometry
  ioctl fails, so an image file can be used as a device without a loop device
  (`fstat` on POSIX, `GetFileSizeEx` on Windows).
- `Account.h` no longer includes `<botan_all.h>`; the public header compiles
  without the Botan build include path.

### 💿 SVOD / GOD

- Enhanced packages are detected by flag bit `0x40` again (the kernel rejects
  bits 0..5 and treats `0x80` as down-level-only), `dataBlockOffset` is a
  single byte unless the package is enhanced, and the one-block shift is
  applied before the data-block index is split into parts. A package whose
  skipped prefix covers the GDF volume descriptor now uses the kernel's alias
  (volume block 16 -> data block 0); previously the whole enhanced mapping
  drifted by one block at every 204-block group and part boundary.
- The GDF volume descriptor is located from `dataBlockOffset` and the enhanced
  flag instead of being hardcoded at `0x12000`/`0x2000`, and `SvodIO` sizes its
  contiguous reads from the level-0 hash-block stride rather than a per-part
  data-start constant.

### 📦 STFS

- Package name lookup now mirrors the kernel: `StfsLookupElementNameInDirectory`
  uses `IoIsNameInExpression`, so matching is case-insensitive and accepts
  `*`/`?`. Internal lookups (`GetFileEntry`, `FileExists`, directory listing)
  use the new `StfsPackage::MatchesFileName`.
- Added the kernel's block geometry inverse,
  `StfsPackage::ComputeNumberOfDataBlocks` (and
  `ComputeAllocatedBlocksForSize`), matching `StfsComputeNumberOfDataBlocks`
  for male volumes; the kernel skips this conversion for female volumes.

### 🕒 FATX

- Written file timestamps match the console codec again. The FAT date/time
  encoder used `tm_year - 4` and masked the year with `0xEF`, so years whose
  `(year - 1980)` has bit 4 set (1996, 2028, 2030, ...) were stored and read
  back 16 years later; it also wrote plain seconds where the kernel stores
  two-second units. The encoder/decoder now match `FatxFatTimestampToTime` /
  `FatxTimeToFatTimestamp` (and the identical STFS pair), verified against the
  16547 kernel and covered by a 1980..2050 round-trip test in
  `tests/smoke.cpp`.

### 🧩 XEX

- Type 1 (raw/zero-based) extraction now zero-pads the block runs to
  `imageSize`, matching `XexpLoadRawImage`. A title image whose runs stop short
  (`Default.xex`: 32 KiB) no longer produces a truncated file; verified on the
  two type 1 samples (29.5 MB encrypted, 71.4 MB plain).
- LZX stored blocks (type 3) work again: the stored path never advanced the
  output counter, so a stored block looped and then failed with "zero block
  length". No corpus image uses stored blocks; `tests/smoke.cpp` now builds one.
- `Xex::IsHeaderHashValid()` verifies the header digest the way
  `XexpVerifyXexHeaders` does, over all 24 corpus modules. It is independent of
  the image key, so it also validates headers whose data cannot be decrypted.
- The LZX decoder takes the window size from the XEX base-file descriptor
  instead of assuming 32 KiB. Retail 17559 `xam.xex` and `dash.xex` use a 1 MiB
  window and 42 position slots (instead of 30); they now decompress to valid
  PE32 images, verified byte-for-byte against an independent decoder.

### 🧍 Avatar assets

- Creating an avatar award package no longer fails at the final rehash. The
  creation, theme, gamer-picture and profile wizards now use the new
  `StfsPackage::RebuildHashes()`, which is also allowed for the LIVE/PIRS
  packages we create ourselves; `Rehash()` keeps refusing retail LIVE/PIRS
  content so its signature is never silently invalidated.
- The package skeleton version now reports "both" when both asset versions are
  injected, instead of always "Natal". The per-asset value inside the STRB is
  unchanged and remains the value the SDK and the console validate.

### 🔑 KeyVault

- A failed resign no longer damages the package. The certificate, header hash
  and signature are built in memory and only written once signing succeeds; a
  wrong or unusable keyvault now throws with the file untouched. Previously the
  failed resign left the rejected keyvault's empty certificate in memory, the
  next rehash wrote it to disk, and the package could no longer be opened
  (`STFS: Invalid console type`).
- Cancelling the keyvault picker in the Package Viewer reports
  "No KeyVault ... the package was not resigned" instead of announcing a
  successful rehash.

### 🔍 Device Content Viewer

- The "Scanning for content..." overlay and the busy bar track their own state,
  so opening the viewer while it is hidden or minimised no longer leaves them
  running.

## Velocity-Next v0.3.2 - 2026-11-18

### 🐞 Extraction

- Closing an ISO or package dialog during a copy no longer frees the device
  under the worker. Both dialog types cancel and join their worker before
  releasing state, refuse a second concurrent extraction, and delete
  themselves when the worker finishes. This is the same lifetime class as #11,
  on the ISO path.

### 🔑 KeyVault

- Cancelling the keyvault picker, or choosing a missing, short or foreign file,
  no longer crashes or silently skips resigning. The resigner rejects an empty
  path and any file that is not 0x4000/0x4010 bytes, the UI warns when nothing
  is selected, and backend errors are shown instead of escaping the event loop.

### 🛡️ Robustness

- Enum values read from packages are validated at the read boundary
  (`BaseIO::ReadEnum` / `EnumTraits`) instead of being cast, so malformed
  headers can no longer produce out-of-range enumerators. STFS magic and
  `MemoryIO` writes are bounds-checked as well.
- LZX: invalid pretree deltas and code lengths above 16 are rejected; a
  fuzzer-found out-of-bounds write in the Huffman table is fixed.

### 🧱 Build

- **MSVC support removed.** Windows builds use the MinGW kit only. The
  `windows-msvc-*` presets, `cmake/msvc-toolchain.cmake`, the MSVC CI job and
  `Velocity-Next-Windows-MSVC.zip` are gone; the remaining Windows artifact is
  `Velocity-Next-Windows.zip`. The library's C++ ABI is compiler-family
  specific, so an MSVC build was never linkable from MinGW consumers or the
  reverse; a C ABI is the planned interoperability path.
- **Qt 6.12 is the minimum.** `QT_MIN_VERSION`, the CI jobs and the Windows
  preset now require Qt 6.12.0 (the releases are built against it).
- **Sanitizer and fuzz gates.** CI runs an ASan/UBSan smoke target plus
  libFuzzer targets for the LZX decoder and the XEX header parser on every push
  and pull request.
- CMake presets require 3.25+ (schema version 6); 3.20+ still works when
  configuring manually.

## Velocity-Next v0.3.1 - 2026-11-18

### 📀 Disc images

- **ISO/XGD directory enumeration**: a multi-sector directory no longer stops
  at the first sector tail. The walk now follows the kernel's
  `OdfxGdfFindNextDirectoryEntry` rule (0xFFFFFFFF end marker → next sector,
  unmarked corrupt tail → stop). On a retail XGD2 image this restores 33 files
  in `Shaders\_ps` and 7 in `AlbumJackets`, and all six retail XGD2/XGD3 test
  images now match a record-level parser (13,376 files).
- **GOD/SVOD directory enumeration**: the same fix for the GDF volume inside a
  GOD package. A complete 30-part GOD package lists 1576 files (previously
  1536) and previously invisible files open and extract.
- **SVOD verification**: the root hash is checked at open and `Verify()` walks
  every data block, level-0 table and the master chain. Verified on a complete
  30-part package.
- ISO 2352-byte Mode 1/2 dumps are detected; XGD1 and 2352-byte images are
  labelled **experimental** in the UI and README.

### 🎮 XEX

- **XCompress/LZX (compression type 2) extraction is implemented.** Retail XEXs
  now decode to the full `imageSize` instead of an empty file. The image key is
  selected from the signed block-0 SHA-1, the digest chain is verified per
  block, and every chunk is decoded with a standalone LZX decoder.
- Delta compression (type 3) remains an explicit unsupported error.

### 🔐 STFS / signatures

- **LIVE/PIRS package signatures are verified**: RSA-2048 PKCS#1 v1.5/SHA-1
  over `[0x22C,0x344)` with the signature byte-reversed. All four corpus PIRS
  packages verify against the retail key.
- Avatar items with skeleton version 0 (retail PIRS) open again and the
  metadata editor preserves the value instead of rewriting it to 1.
- SVOD `dataBlockCount`/`dataBlockOffset` are read big-endian as u24, matching
  the kernel.

### 🐞 Extraction

- **Saved-game extraction no longer crashes with `std::bad_alloc`.** The
  extraction dialog runs on a worker thread and outlives the caller, but the
  "Extract Selected" path kept pointers into a temporary `QList<StfsFileEntry>`;
  the worker then read freed entries. File entries are now owned by value, and
  the progress calculation no longer reinterprets the wrong entry type (#11).
- Choosing a folder as the destination for a single file now writes the file
  inside it instead of failing with "is a directory".
- A file whose size exceeds its allocated block count is rejected with a clear
  error instead of overrunning the extraction buffer.

### 🖥️ Device and content

- Content tree and device UI crash fixes: the tree is enabled when devices
  finish loading, SVOD ownership is not freed twice, and profile/title export
  to local disk no longer segfaults.
- Background device loading with an overlay and status-bar progress; the
  overlay is non-dismissible and closing the window waits for the loader.
- Profiles and titles export as folder trees or flat files.
- Device reads are cached in aligned 64 KiB windows, with correct handling of
  reads at or past the device end.
- The Qt 6.10 private-include build path is fixed and the tree is warning-free
  on GCC, Clang and MSVC.

---
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
