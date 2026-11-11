# Building Velocity-Next

## Prerequisites

- **Qt 6.7.3 or higher** (Components: Core, Xml, Widgets, Network)
  - The package browser uses `qzipreader_p.h`. Qt 6.10 stopped exporting the
    private include directories from `Qt6::Core`, so CMake requests
    `CorePrivate` when the installation provides it (Qt 6.10+) and falls back
    to `Qt6::Core` include paths on older releases
  - On Linux the private headers may be a separate package
    (`qt6-base-private-dev` on Debian/Ubuntu, `qt6-qtbase-private-devel` on
    Fedora, included in `qt6-base` on Arch)
  - Verified by building against Qt 6.7.3, 6.8.3, 6.10.3 and 6.12.0
- **CMake 3.20+**
- **C++20 compiler**:
  - **Windows**: MinGW 13.1.0+ or MSVC 2022 (Visual Studio 17.0+)
  - **macOS**: Clang (Xcode Command Line Tools)
  - **Linux**: GCC 11+ or Clang 14+
- **Python 3** (for Botan amalgamation build)

## Quick Start

```bash
# Clone the repository
git clone https://github.com/Mordaur/Velocity-Next
cd Velocity-Next

# Configure (choose one preset)
cmake --preset windows-mingw-release    # Windows MinGW Release
cmake --preset windows-mingw-debug      # Windows MinGW Debug
cmake --preset windows-msvc-release     # Windows MSVC Release
cmake --preset windows-msvc-debug       # Windows MSVC Debug
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

- **Windows MinGW** - Produces portable .zip with Qt dependencies bundled
- **Windows MSVC** - MSVC 2022 build for maximum compatibility
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

See the [Releases page](https://github.com/Mordaur/Velocity-Next/releases). No
release has been published yet, so until v0.3.0 is tagged build from source with
the presets above.

## Botan Dependency

The cryptography library is automatically handled:

- **Automatic Download**: Botan is automatically downloaded and built
- **No Submodule Required**: No git submodules to initialize
- **Custom Modules**: Uses minimal module set defined in botan_modules.cmake
- **Amalgamation**: Built as single-file amalgamation for better performance

## Qt Configuration

### Qt Version Information

- **Minimum Required**: Qt 6.7.3
- **Tested Versions**: 6.7.3, 6.8.3 (no CorePrivate), 6.10.3 (CI), 6.12 (development)
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
# Windows MinGW Release
cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release

# Windows MSVC Release
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release

# macOS Release
cmake --preset macos-release
cmake --build --preset macos-release

# Linux Release
cmake --preset linux-release
cmake --build --preset linux-release
```

### Debug Builds (For development)

```bash
# Windows MinGW Debug
cmake --preset windows-mingw-debug
cmake --build --preset windows-mingw-debug

# Windows MSVC Debug
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug

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

## Platform-Specific Notes

### Windows

#### MinGW
- **Presets**: `windows-mingw-release` or `windows-mingw-debug`
- **Compiler**: MinGW 13.1.0+ (must match Qt installation)
- **Qt Installation**: Use Qt Online Installer and select MinGW kit
- **Output**: Executable with Windows resource data

#### MSVC (Visual Studio 2022)
- **Presets**: `windows-msvc-release` or `windows-msvc-debug`
- **Requirements**: Visual Studio 2022 (17.0+) with C++ workload
- **Qt Installation**: Use Qt Online Installer and select MSVC 2022 64-bit kit
- **Setup**: Run from **Developer Command Prompt for VS 2022** or **Developer PowerShell for VS 2022**
- **Output**: Executable with Windows resource data

**MSVC Build Example:**
```bash
# Open Developer Command Prompt for VS 2022 or Developer PowerShell for VS 2022
# Then run:
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
```

**Note**: MSVC builds require:
- Visual Studio 2022 or Build Tools for Visual Studio 2022
- C++ CMake tools for Windows component
- Ninja build system (included with Visual Studio)
- Qt compiled for MSVC 2022 (download from Qt installer)

### macOS
- **Bundle**: Creates .app bundle with proper metadata
- **Icons**: Uses velocity.icns for application icon
- **Output**: Velocity.app bundle

#### macOS Distribution

**Prerequisites:**
- Qt 6.7.3 or higher installed on your system
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

## Troubleshooting

### Qt Not Found

```bash
# Verify Qt installation (must be 6.7.3 or higher)
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

There is **no elevation helper on macOS yet** — `velocity-device-helper` can be
built but the GUI has no way to start it (it relies on `pkexec`/UAC elsewhere).
Run the application itself with `sudo`:

```bash
sudo ./out/build/macos-release/VelocityNext.app/Contents/MacOS/VelocityNext
```

**Note**: Xbox 360 drives appear as whole-disk nodes (`/dev/disk*`); Velocity
enumerates them and opens the raw counterpart (`/dev/rdisk*`). Drive detection
on macOS is a known gap tracked in the issue tracker.

---

For more details, see the project documentation or check the CMakeLists.txt files for advanced configuration options.