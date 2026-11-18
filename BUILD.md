# Building Velocity-Next

## Prerequisites

- **Qt 6.12.0 or higher** (Components: Core, Xml, Widgets, Network)
  - The package browser uses `qzipreader_p.h`. Qt 6.10 stopped exporting the
    private include directories from `Qt6::Core`, so CMake requests
    `CorePrivate` when the installation provides it (Qt 6.10+) and falls back
    to `Qt6::Core` include paths on older releases
  - On Linux the private headers may be a separate package
    (`qt6-base-private-dev` on Debian/Ubuntu, `qt6-qtbase-private-devel` on
    Fedora, included in `qt6-base` on Arch)
  - Qt 6.12.0 is the minimum and the version used by CI and the releases
- **CMake 3.25+** (the presets use schema version 6; 3.20+ works when configuring
  manually without `--preset`)
- **C++20 compiler**:
  - **Windows**: MinGW 13.1.0+ (Qt's MinGW kit; MSVC is not supported)
  - **macOS**: Clang (Xcode Command Line Tools)
  - **Linux**: GCC 11+ or Clang 14+
- **Python 3** (for Botan amalgamation build)

## Quick Start

```bash
# Clone the repository
git clone https://github.com/Mordaur/Velocity-Next
cd Velocity-Next

# Configure (choose one preset)
cmake --preset windows-mingw-release    # Windows Release
cmake --preset windows-mingw-debug      # Windows Debug
cmake --preset macos-release            # macOS Release  
cmake --preset macos-debug              # macOS Debug
cmake --preset linux-release            # Linux Release
cmake --preset linux-debug              # Linux Debug

# Build
cmake --build --preset [chosen-preset]
```

## Automated Builds (GitHub Actions)

Prefer not to build locally? Every workflow can be run manually from the
Actions tab (they are `workflow_dispatch`), and a `v*` tag triggers the full
matrix build followed by a release.

### Available Workflows

- **Windows (MinGW)** - Produces portable .zip with Qt dependencies bundled
- **Linux** - Ubuntu build with AppImage support (planned)
- **macOS** - .app bundle and DMG installer

### Triggering Builds

1. **Fork the repository** on GitHub
2. **Enable Actions** in your fork (Settings → Actions → General)
3. **Manual builds**: Go to Actions tab → Select workflow → "Run workflow"
4. **Automated releases**: Push a version tag (e.g., `v0.3.0`); `build-matrix.yml`
   builds every platform and `release.yml` publishes the artifacts under the tag
   derived from `project(VelocityNext VERSION ...)`

### Downloading Pre-built Releases

See the [Releases page](https://github.com/Mordaur/Velocity-Next/releases).
**v0.3.2** is the latest release; it publishes the Windows zip, the macOS DMG
and the Linux tarball. Every archive keeps `velocity-device-helper` next to the
executable. The Linux tarball does not bundle Qt — install Qt 6.12.0 on the
host, or build from source with the presets above.

## Botan Dependency

The cryptography library is automatically handled:

- **Automatic Download**: Botan is automatically downloaded and built
- **No Submodule Required**: No git submodules to initialize
- **Custom Modules**: Uses minimal module set defined in botan_modules.cmake
- **Amalgamation**: Built as single-file amalgamation for better performance

## Qt Configuration

### Qt Version Information

- **Minimum Required**: Qt 6.12.0
- **Tested Versions**: 6.12.0 (CI and minimum)
- **Components Required**: Core, Xml, Widgets, Network

### Automatic Discovery

The build system uses a three-stage process to find Qt automatically:

1. **CMake Native Detection** - Uses standard CMake Qt discovery
2. **qmake Fallback** - Finds qmake6/qmake in PATH and queries installation  
3. **Environment Variable** - Checks QT6_PREFIX_PATH if needed

**Common locations where Qt is found automatically:**

- **Windows**: Standard Qt installer paths (C:/Qt/[version]/*)
- **macOS**: Qt installer locations (~/Qt) and Homebrew installations  
- **Linux**: Qt installer locations (~/Qt) and system package locations

The detection works with any Qt installation as long as:
- Qt is installed in a standard location, OR
- `qmake6` is available in your PATH

If Qt is not found automatically, see "Manual Qt Configuration" below.

### Manual Qt Configuration

If Qt is not found automatically, specify it manually using CMAKE_PREFIX_PATH:

```bash
# Set Qt path explicitly
cmake -B build -DCMAKE_PREFIX_PATH="/path/to/your/qt"
cmake --build build
```

## Build Options

### Release Builds (Recommended for end users)

```bash
# Windows Release
cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release

# macOS Release
cmake --preset macos-release
cmake --build --preset macos-release

# Linux Release
cmake --preset linux-release
cmake --build --preset linux-release
```

### Debug Builds (For development)

```bash
# Windows Debug
cmake --preset windows-mingw-debug
cmake --build --preset windows-mingw-debug

# macOS Debug
cmake --preset macos-debug
cmake --build --preset macos-debug

# Linux Debug
cmake --preset linux-debug
cmake --build --preset linux-debug
```

### Library Configuration

#### Shared Library (Default)
```bash
# Build XboxInternals as shared library (default - no flags needed)
cmake --preset windows-mingw-release
```
- **Windows**: Creates `libXboxInternals.dll` (automatically copied to Velocity output directory)
- **macOS**: Creates `libXboxInternals.dylib` (handled by macOS bundle system)
- **Linux**: Creates `libXboxInternals.so` (system finds via rpath)
- **Note**: Shared library is automatically placed alongside the executable - no manual copying needed

#### Static Library
```bash
# Build only static library
cmake --preset windows-mingw-release -DBUILD_XBOXINTERNALS_SHARED=OFF
```
- Creates `libXboxInternals.a` (all platforms)
- Library code is compiled directly into VelocityNext executable
- No separate DLL/shared library needed at runtime
- Results in larger executable but simpler distribution (single file)
- **Note**: When shared is OFF, static is automatically enabled

#### Both Shared and Static
```bash
# Build both shared and static versions (for library distribution)
cmake --preset windows-mingw-release -DBUILD_XBOXINTERNALS_STATIC=ON
```
- Creates both `libXboxInternals.dll` and `libXboxInternals.a`
- Velocity executable uses the shared version
- Useful for developers who want both options available

### Botan Version

```bash
# Test with different Botan version (if needed)
cmake --preset windows-mingw-release -DBOTAN_VERSION=3.14.1
```

### Sanitizer and Fuzzer Builds

Runtime instrumentation is behind `-DSANITIZERS` (empty by default, so release
builds are unchanged):

```bash
# AddressSanitizer + UndefinedBehaviorSanitizer, with the in-tree smoke test
cmake -S . -B build/sanitize -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSANITIZERS=address,undefined \
  -DQT_FIND_PRIVATE_MODULES=ON
cmake --build build/sanitize --target xbi_smoke -j
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/sanitize --output-on-failure

# ThreadSanitizer uses the same switch: -DSANITIZERS=thread

# libFuzzer (Clang only). The library needs coverage instrumentation, so the
# fuzzer runtime itself is only added on the target.
cmake -S . -B build/fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
  -DSANITIZERS=address,undefined -DBUILD_FUZZERS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link"
cmake --build build/fuzz --target lzx_fuzz xex_fuzz -j
./build/fuzz/tests/lzx_fuzz -runs=50000 -max_len=8192
./build/fuzz/tests/xex_fuzz -dict=tests/fuzz/xex.dict -runs=200000 -max_len=8192
```

`xex_fuzz` parses arbitrary input as a complete XEX through `MemoryIO`, including
the optional-header, section and rating tables. `lzx_fuzz` feeds the LZX decoder.

`tests/` in this repository contains only the smoke target and the LZX fuzzer
that CI can run on a bare runner. The corpus, differential and mutation harness
lives outside the checkout.

## Platform-Specific Notes

### Windows

- **Presets**: `windows-mingw-release` or `windows-mingw-debug`
- **Compiler**: MinGW 13.1.0+ (must match the Qt installation)
- **Qt Installation**: Qt Online Installer → MinGW kit
- **Output**: Executable with Windows resource data
- **MSVC is not supported**: Windows builds use the MinGW kit, which is what the
  release zip ships. An MSVC build would need its own Qt kit and produces a
  `libXboxInternals.dll` that is ABI-incompatible with the MinGW one.

### macOS
- **Bundle**: Creates .app bundle with proper metadata
- **Icons**: Uses velocity.icns for application icon
- **Output**: Velocity.app bundle

#### macOS Distribution

**Prerequisites:**
- Qt 6.12.0 or higher installed on your system
- Xcode command line tools installed
- Build the project first using the presets above
- `app.entitlements` file exists in project root

**Note**: The deployment script will automatically find Qt in:
- Your PATH
- QT6_PREFIX_PATH environment variable  
- Common installation locations (~/Qt, Homebrew, etc.)

If needed, you can explicitly set your Qt path:

```bash
export PATH="$HOME/Qt/[version]/macos/bin:$PATH"
# or
export QT6_PREFIX_PATH="/path/to/your/qt"

# Make the deployment script executable (first time only)
chmod +x deploy_mac.sh

# Deploy and sign (from project root)
./deploy_mac.sh

# For debug builds
./deploy_mac.sh out/build/macos-debug
```

**Note**: Uses ad-hoc signing by default. For distribution, replace `-` with your Developer ID in the script.

### Linux
- **Standard**: Creates standard Linux executable
- **Output**: Velocity binary

## Editor Setup (VS Code / clangd)

The build is plain CMake + Ninja, so any editor that consumes a compile database
works. Dropping MSVC does not cost IntelliSense or debugging: VS Code drives an
editor-side parser (the C/C++ extension or clangd) and GDB/LLDB, none of which
needs `cl.exe`.

### Compile database

`CMAKE_EXPORT_COMPILE_COMMANDS` is already on, so configuring a preset writes
`compile_commands.json` into its build directory:

| Platform | Database |
|---|---|
| Windows | `out/build/windows-mingw-release/compile_commands.json` |
| macOS | `out/build/macos-release/compile_commands.json` |
| Linux | `out/build/linux-release/compile_commands.json` |

If you configure outside the checkout (for example `-B build/linux-release` from
a parent directory), point the editor at that directory instead.

**Option A — Microsoft C/C++ extension** (`.vscode/c_cpp_properties.json`):

```jsonc
{
  "version": 4,
  "configurations": [
    {
      "name": "Windows (MinGW)",
      "compileCommands": "${workspaceFolder}/out/build/windows-mingw-release/compile_commands.json",
      "intelliSenseMode": "windows-gcc-x64"
    },
    {
      "name": "Linux (GCC)",
      "compileCommands": "${workspaceFolder}/out/build/linux-release/compile_commands.json",
      "intelliSenseMode": "linux-gcc-x64"
    },
    {
      "name": "macOS (Clang)",
      "compileCommands": "${workspaceFolder}/out/build/macos-release/compile_commands.json",
      "intelliSenseMode": "macos-clang-arm64"
    }
  ]
}
```

**Option B — clangd**, usually more accurate on a large C++ tree. It searches
upwards from each source file for `compile_commands.json`; when the database
lives in the build tree, add a `.clangd` at the repository root:

```yaml
CompileFlags:
  CompilationDatabase: out/build/linux-release
```

(or pass `--compile-commands-dir=` to clangd).

Two things that cause phantom editor errors:

- **Use the same toolchain the database was generated with.** An MSVC database
  against a MinGW build (or the reverse) evaluates different `_WIN32`/`_MSC_VER`
  branches; MSVC is not a supported toolchain here.
- **Configure with `-DQT_FIND_PRIVATE_MODULES=ON`.** Without it
  `${Qt6Core_PRIVATE_INCLUDE_DIRS}` is empty, so `Velocity/zipviewer.cpp`
  (which includes `qzipreader_p.h`) shows a missing header in the editor even
  though a correctly configured build succeeds.

Re-run CMake after adding or removing source files so the database stays current.

### Debugging

Use GDB on Windows/Linux and LLDB (or GDB) on macOS. On Windows, use the GDB that
matches the Qt MinGW kit (`mingw1310_64`) and put its `bin` directory on `PATH`
so `libstdc++-6.dll`, `libgcc_s_seh-1.dll` and `libwinpthread-1.dll` resolve
when the debugger launches.

`.vscode/launch.json` (Windows example; adjust the Qt paths to your kit):

```jsonc
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "VelocityNext (MinGW + gdb)",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/out/build/windows-mingw-release/VelocityNext/VelocityNext.exe",
      "cwd": "${workspaceFolder}/out/build/windows-mingw-release/VelocityNext",
      "MIMode": "gdb",
      "miDebuggerPath": "C:/Qt/Tools/mingw1310_64/bin/gdb.exe",
      "environment": [
        {
          "name": "PATH",
          "value": "C:/Qt/6.12.0/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;${env:PATH}"
        }
      ],
      "setupCommands": [
        { "text": "-enable-pretty-printing", "ignoreFailures": true }
      ]
    }
  ]
}
```

Qt Creator works with the same kit (Qt MinGW + CMake + GDB) and has better Qt
integration if you prefer a full IDE.

### What is still MSVC-only

`/analyze`, the Visual Studio profiler/PIX integration and `cppvsdbg` belong to
the MSVC toolchain, not to VS Code; they are not part of this project's toolchain
any more. Static analysis comes from clang-tidy/clangd, runtime checks from the
Linux sanitizer CI job, and Windows behaviour from manual runs on the built
binary (MinGW's ASan/UBSan support is uneven).

## Troubleshooting

### Qt Not Found

```bash
# Verify Qt installation (must be 6.12.0 or higher)
qmake6 --version

# Set Qt path explicitly
cmake -B build -DCMAKE_PREFIX_PATH="/path/to/qt"
```

### Botan Build Issues
```bash
# Clean and rebuild
rm -rf out/build
cmake --preset your-preset

# Ensure Python 3 is available
python --version   # Windows
python3 --version  # macOS/Linux

# Check internet connection (Botan download requires internet)
```
### Compiler Issues
- Ensure your compiler supports C++20
- Match Qt kit with your compiler (MinGW Qt with MingGW compiler)

## Output Locations

Build outputs are organized in:
- `out/build/windows-mingw-release/` - Windows MinGW Release
- `out/build/windows-mingw-debug/` - Windows MinGW Debug
- `out/build/macos-release/` - macOS Release
- `out/build/macos-debug/` - macOS Debug
- `out/build/linux-release/` - Linux Release
- `out/build/linux-debug/` - Linux Debug

## Running Velocity

### Windows - Raw Device Access

Velocity starts normally (as invoker). Browsing uses read-only raw access where
Windows permits it. When a device probe is denied, the app launches
`velocity-device-helper.exe` (shipped next to `VelocityNext.exe`) through a UAC
prompt; only the helper runs elevated, and it exits when the viewer closes.

**Manual fallback** (if the helper cannot start, for example on a machine where
UAC is disabled or the helper was not copied):

1. Navigate to the VelocityNext executable:
   - Release: `out/build/windows-mingw-release/VelocityNext/VelocityNext.exe`
   - Debug: `out/build/windows-mingw-debug/VelocityNext/VelocityNext.exe`

2. **Right-click** `VelocityNext.exe` → **"Run as administrator"**

You can also create an administrator shortcut: right-click the executable → **Create shortcut** → shortcut **Properties** → **Advanced** → **Run as administrator**.

Declining the UAC prompt leaves the device list empty and shows
"Administrator access was not granted" — that is expected, not a detection bug.

### Linux

On Linux, raw block devices are normally `root:disk` with mode `0660`. Velocity
starts `velocity-device-helper` through `pkexec` (or passwordless `sudo` when it
is available) as soon as a probe is denied, so normally no manual step is
required.

**Option 1: Run with sudo** (manual fallback for testing or one-off use)
```bash
sudo ./out/build/linux-release/VelocityNext/VelocityNext
```

**Option 2: Add user to the disk group** (for regular use; deploy via local or central group policy)
```bash
sudo usermod -a -G disk $USER
# Log out and log back in
```

**Note**: Xbox 360 drives connected via USB should appear as `/dev/sd*` devices (e.g., `/dev/sdb`).

### macOS

On macOS, raw disk devices (`/dev/rdisk*`) can only be opened by root.

macOS has **experimental** helper support: when a probe is denied, Velocity asks
the system to run `velocity-device-helper` with administrator privileges
(`osascript … with administrator privileges`). This path has not been verified on
real hardware. If the prompt does not appear, or the drive is still missing, run
the application itself with `sudo`:

```bash
sudo ./out/build/macos-release/VelocityNext.app/Contents/MacOS/VelocityNext
```

**Note**: Xbox 360 drives appear as whole-disk nodes (`/dev/disk*`); Velocity
enumerates them and opens the raw counterpart (`/dev/rdisk*`). Drive detection
on macOS is a known gap tracked in the issue tracker.

---

For more details, see the project documentation or check the CMakeLists.txt files for advanced configuration options.