# ===================================
# Velocity-Next - CMake superproject
# ===================================
# Project structure:
#   cmake/          - CMake modules (FindBotan.cmake, botan_modules.cmake, etc.)
#   Velocity/       - Main GUI application
#   XboxInternals/  - Xbox file format library
#   botan/          - Cryptography library (Git submodule, optional)
#   out/build/      - Generated build outputs (not in source control)
#   specs/          - Feature specifications and documentation
#
# Botan CMake integration adapted from: https://github.com/Tectu/botan-cmake
cmake_minimum_required(VERSION 3.20)

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "
In-source builds are not allowed!

Please create a separate build directory and run CMake from there:

  cmake -B out/build/your-preset
  cmake --build out/build/your-preset

Or use a preset:

  cmake --preset windows-mingw-release
  cmake --build --preset windows-mingw-release

To clean up, remove: CMakeCache.txt and CMakeFiles/
")
endif()

project(VelocityNext VERSION 0.3.0 LANGUAGES CXX)

# Display build configuration for debugging
message(STATUS "Generator: ${CMAKE_GENERATOR}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
if(CMAKE_GENERATOR_PLATFORM)
    message(STATUS "Platform: ${CMAKE_GENERATOR_PLATFORM}")
endif()

# Export compilation database for clang-tidy / clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use folders in IDE (Visual Studio) for cleaner grouping
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Build System Strategy:
# - All presets use the Ninja generator (including MSVC) for consistency across platforms
# - This provides faster builds and uniform behavior between MinGW/MSVC/GCC/Clang
# - If you prefer the Visual Studio generator for IDE integration:
#   * Use: cmake -G "Visual Studio 17 2022" -A x64 -S . -B build
#   * Note: VS generator uses multi-config (Debug/Release in same tree)
#   * Presets use single-config (separate directories per build type)

# Platform-specific settings
if(APPLE)
    set(PLATFORM_BUILD_NAME "OSX" CACHE STRING "Platform build name")
elseif(UNIX AND NOT APPLE)
    set(PLATFORM_BUILD_NAME "Unix" CACHE STRING "Platform build name") 
elseif(WIN32)
    set(PLATFORM_BUILD_NAME "Win" CACHE STRING "Platform build name")
endif()

# Enforce C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Create an INTERFACE library to hold common compile options
add_library(velocity_compiler_flags INTERFACE)

# Ensure MSVC reports correct __cplusplus value for C++20 compliance
if(MSVC)
    target_compile_options(velocity_compiler_flags INTERFACE /Zc:__cplusplus)
endif()

# All Velocity targets (XboxInternals, Velocity) must link against velocity_compiler_flags
# to inherit common compile options (e.g., MSVC /Zc:__cplusplus for C++20 compliance)
# Example: target_link_libraries(MyTarget PRIVATE velocity_compiler_flags)

# ------------------------------
# Options
# ------------------------------
# XboxInternals library build configuration:
# - Default: BUILD_XBOXINTERNALS_SHARED=ON (shared library only)
# - Both ON: Builds both shared and static variants
# - Both OFF: Automatically enables STATIC to ensure library is built
option(BUILD_XBOXINTERNALS_SHARED "Build XboxInternals as a shared library" ON)
option(BUILD_XBOXINTERNALS_STATIC "Also build XboxInternals as a static library" OFF)

# Ensure at least one library type is built
if(NOT BUILD_XBOXINTERNALS_SHARED AND NOT BUILD_XBOXINTERNALS_STATIC)
    message(STATUS "Neither SHARED nor STATIC selected for XboxInternals; defaulting to STATIC")
    set(BUILD_XBOXINTERNALS_STATIC ON)
endif()

# Botan version control
set(BOTAN_VERSION "3.14.1" CACHE STRING "Botan version to fetch/use")

# ====================================================================
# BOTAN DEPENDENCY MANAGEMENT
# ====================================================================
# 
# Botan (cryptography library) is handled via two possible sources:
#
# 1. GIT SUBMODULE (Developer/CI workflow):
#    - Location: ${CMAKE_SOURCE_DIR}/botan/
#    - Used when: The submodule is initialized and contains configure.py
#    - Benefits: Fast, version-controlled, no download needed
#
# 2. AUTOMATIC DOWNLOAD (End-user workflow):
#    - Location: ${CMAKE_BINARY_DIR}/botan/
#    - Used when: Submodule is not present (fresh clone without --recurse-submodules)
#    - Benefits: No manual steps for end users
#
# The FindBotan.cmake module automatically chooses between these options.
# ====================================================================

# Primary location for Botan Git submodule (if available)
# This is the preferred source for developers and CI systems
set(BOTAN_SUBMODULE_DIR "${CMAKE_SOURCE_DIR}/botan" 
    CACHE PATH 
    "Path to Botan Git submodule at repository root. Used when submodule is initialized."
)

# Fallback download location for Botan source
# Used when submodule is not available (end-user builds)
set(BOTAN_FALLBACK_DIR "${CMAKE_BINARY_DIR}/botan" 
    CACHE PATH 
    "Where to download Botan tarball if Git submodule is absent. Used for end-user builds."
)

# Qt setup
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ------------------------------
# Qt Discovery
# ------------------------------
set(QT_MIN_VERSION "6.10.0")

# Primary method: Use CMake's built-in Qt discovery
find_package(Qt6 ${QT_MIN_VERSION} COMPONENTS Core CorePrivate Xml Widgets Network)

if(NOT Qt6_FOUND)
    # Secondary method: Try qmake-based discovery
    find_program(QMAKE_EXECUTABLE NAMES qmake6 qmake-qt6 qmake)
    if(QMAKE_EXECUTABLE)
        message(STATUS "Qt6 not found via CMake, trying qmake discovery...")
        execute_process(
            COMMAND ${QMAKE_EXECUTABLE} -query QT_INSTALL_PREFIX
            OUTPUT_VARIABLE QT_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(QT_PREFIX AND EXISTS "${QT_PREFIX}")
            message(STATUS "Found Qt via qmake at: ${QT_PREFIX}")
            set(CMAKE_PREFIX_PATH ${QT_PREFIX} ${CMAKE_PREFIX_PATH})
            find_package(Qt6 ${QT_MIN_VERSION} QUIET COMPONENTS Core CorePrivate Xml Widgets Network)
        endif()
    endif()
endif()

if(NOT Qt6_FOUND)
    # Final method: Check common environment variables
    if(DEFINED ENV{QT6_PREFIX_PATH})
        message(STATUS "Trying QT6_PREFIX_PATH environment variable: $ENV{QT6_PREFIX_PATH}")
        set(CMAKE_PREFIX_PATH $ENV{QT6_PREFIX_PATH} ${CMAKE_PREFIX_PATH})
        find_package(Qt6 ${QT_MIN_VERSION} QUIET COMPONENTS Core CorePrivate Xml Widgets Network)
    endif()
endif()

if(NOT Qt6_FOUND)
    message(FATAL_ERROR "
Qt6 not found! Please install Qt6 (version ${QT_MIN_VERSION} or higher).

Detection methods tried:
1. CMake's built-in Qt discovery
2. qmake query (qmake6, qmake-qt6, qmake)
3. QT6_PREFIX_PATH environment variable

Solutions:
- Install Qt6 from https://www.qt.io/download
- Ensure Qt6 binaries are in your PATH
- Set QT6_PREFIX_PATH environment variable
- Set CMAKE_PREFIX_PATH: cmake -B build -DCMAKE_PREFIX_PATH=/path/to/qt
")
endif()

message(STATUS "Found Qt6 ${Qt6Core_VERSION} at: ${Qt6Core_DIR}")

# Find Qt6Concurrent separately (needed for isodialog.cpp)
find_package(Qt6 COMPONENTS Concurrent REQUIRED)

# Make cmake/ available
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ====================================================================
# BOTAN INTEGRATION
# ====================================================================
# The FindBotan.cmake module will:
# 1. Check if BOTAN_SUBMODULE_DIR exists and contains configure.py
# 2. If yes: Use the submodule as Botan source
# 3. If no:  Download Botan ${BOTAN_VERSION} to BOTAN_FALLBACK_DIR
# 4. Generate amalgamated build (botan_all.cpp/h) with only required modules
# ====================================================================

if(EXISTS "${BOTAN_SUBMODULE_DIR}/configure.py")
  message(STATUS "Using Botan submodule at: ${BOTAN_SUBMODULE_DIR}")
  set(Botan_PATH "${BOTAN_SUBMODULE_DIR}")
else()
  message(STATUS "Botan submodule not found, will download to: ${BOTAN_FALLBACK_DIR}")
  set(Botan_PATH "")  # Empty triggers download in FindBotan.cmake
  set(BOTAN_DOWNLOAD_DIR "${BOTAN_FALLBACK_DIR}")
endif()

# Locate Botan source and generate amalgamation
find_package(Botan ${BOTAN_VERSION} REQUIRED)

# ====================================================================
# CHECK FOR NEWER BOTAN VERSIONS (ONLINE)
# ====================================================================
# Query GitHub API to notify users of available updates
# Skipped if curl is not available or network request fails
# ====================================================================
find_program(CURL_EXECUTABLE curl)
if(CURL_EXECUTABLE)
    message(STATUS "Checking for Botan updates...")
    execute_process(
        COMMAND ${CURL_EXECUTABLE} -s https://api.github.com/repos/randombit/botan/tags?per_page=1
        OUTPUT_VARIABLE BOTAN_LATEST_JSON
        ERROR_QUIET
        RESULT_VARIABLE CURL_RESULT
        TIMEOUT 5
    )
    
    if(CURL_RESULT EQUAL 0 AND BOTAN_LATEST_JSON)
        # Parse JSON for first "name": "3.x.x" (latest tag)
        string(REGEX MATCH "\"name\"[^\"]*\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _ "${BOTAN_LATEST_JSON}")
        set(BOTAN_LATEST_VERSION "${CMAKE_MATCH_1}")
        
        if(BOTAN_LATEST_VERSION AND BOTAN_LATEST_VERSION VERSION_GREATER BOTAN_VERSION)
            message(STATUS "")
            message(STATUS "===============================================")
            message(STATUS "  Newer Botan version available!")
            message(STATUS "  Current:   ${BOTAN_VERSION}")
            message(STATUS "  Available: ${BOTAN_LATEST_VERSION}")
            message(STATUS "")
            message(STATUS "  To upgrade:")
            message(STATUS "  1. Update BOTAN_VERSION in CMakeLists.txt")
            message(STATUS "  2. Or run: cmake -DBOTAN_VERSION=${BOTAN_LATEST_VERSION} ..")
            message(STATUS "  3. Download: https://github.com/randombit/botan/archive/refs/tags/${BOTAN_LATEST_VERSION}.tar.gz")
            message(STATUS "===============================================")
            message(STATUS "")
        elseif(BOTAN_LATEST_VERSION VERSION_EQUAL BOTAN_VERSION)
            message(STATUS "Botan ${BOTAN_VERSION} is up to date")
        endif()
    endif()
endif()

# Minimal modules required by Velocity/XboxInternals
include(cmake/botan_modules.cmake)

# Create an amalgamated botan static library for ALL platforms
botan_generate(
  BotanAmalgamation
    ${VELOCITY_BOTAN_MODULES}
)

# Install Botan amalgamation header for standalone XboxInternals distribution
# This allows consumers to use XboxInternals without rebuilding Botan
include(GNUInstallDirs)
install(FILES ${CMAKE_BINARY_DIR}/botan_all.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/botan
  OPTIONAL  # File might not exist until build time
)

# ====================================================================
# BUILD CONFIGURATION SUMMARY
# ====================================================================
# Display a comprehensive summary of the build configuration
# ====================================================================

# Capture configuration start time
string(TIMESTAMP CONFIG_START_TIME "%s")

# Detect compiler name and version
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(COMPILER_NAME "GCC")
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpversion OUTPUT_VARIABLE COMPILER_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(COMPILER_NAME "MSVC")
    set(COMPILER_VERSION ${MSVC_VERSION})
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(COMPILER_NAME "Clang")
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} --version OUTPUT_VARIABLE CLANG_VERSION_OUTPUT)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" COMPILER_VERSION "${CLANG_VERSION_OUTPUT}")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
    set(COMPILER_NAME "Apple Clang")
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} --version OUTPUT_VARIABLE CLANG_VERSION_OUTPUT)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" COMPILER_VERSION "${CLANG_VERSION_OUTPUT}")
else()
    set(COMPILER_NAME ${CMAKE_CXX_COMPILER_ID})
    set(COMPILER_VERSION "unknown")
endif()

# Detect platform name
if(WIN32)
    set(PLATFORM_NAME "Windows")
elseif(APPLE)
    set(PLATFORM_NAME "macOS")
elseif(UNIX)
    set(PLATFORM_NAME "Linux")
else()
    set(PLATFORM_NAME ${CMAKE_SYSTEM_NAME})
endif()

# Build library types
set(LIBRARY_TYPES "")
if(BUILD_XBOXINTERNALS_SHARED)
    list(APPEND LIBRARY_TYPES "Shared")
endif()
if(BUILD_XBOXINTERNALS_STATIC)
    list(APPEND LIBRARY_TYPES "Static")
endif()
string(REPLACE ";" ", " LIBRARY_TYPES_STR "${LIBRARY_TYPES}")

# Get Python version (used by Botan configure.py)
find_package(Python COMPONENTS Interpreter)
if(Python_FOUND)
    set(PYTHON_VERSION_STR "${Python_VERSION}")
    
    # Check for newer Python versions online via GitHub API
    if(CURL_EXECUTABLE)
        execute_process(
            COMMAND ${CURL_EXECUTABLE} -s https://api.github.com/repos/python/cpython/tags?per_page=100
            OUTPUT_VARIABLE PYTHON_TAGS_JSON
            ERROR_QUIET
            TIMEOUT 5
        )
        
        # Parse for latest stable Python 3.x version (v3.x.x format)
        # Filter out alpha/beta/rc releases by looking for clean version tags
        string(REGEX MATCHALL "\"name\"[^\"]*\"v([0-9]+\\.[0-9]+\\.[0-9]+)\"" PYTHON_VERSIONS "${PYTHON_TAGS_JSON}")
        
        # Extract just version numbers and find the latest
        set(PYTHON_LATEST_VERSION "")
        foreach(VERSION_MATCH ${PYTHON_VERSIONS})
            string(REGEX MATCH "v([0-9]+\\.[0-9]+\\.[0-9]+)" _ "${VERSION_MATCH}")
            set(VERSION_NUM "${CMAKE_MATCH_1}")
            if(NOT PYTHON_LATEST_VERSION OR VERSION_NUM VERSION_GREATER PYTHON_LATEST_VERSION)
                set(PYTHON_LATEST_VERSION "${VERSION_NUM}")
            endif()
        endforeach()
        
        if(PYTHON_LATEST_VERSION AND PYTHON_LATEST_VERSION VERSION_GREATER Python_VERSION)
            set(PYTHON_UPDATE_STATUS "⚠ (${PYTHON_LATEST_VERSION} available)")
        elseif(PYTHON_LATEST_VERSION VERSION_EQUAL Python_VERSION)
            set(PYTHON_UPDATE_STATUS "✓ (up to date)")
        else()
            set(PYTHON_UPDATE_STATUS "✓")
        endif()
    else()
        set(PYTHON_UPDATE_STATUS "✓")
    endif()
else()
    set(PYTHON_VERSION_STR "not found")
    set(PYTHON_UPDATE_STATUS "✗")
endif()

# Check for ccache (compiler cache for faster rebuilds)
find_program(CCACHE_EXECUTABLE ccache)
if(CCACHE_EXECUTABLE)
    set(CCACHE_STATUS "✓ detected (faster rebuilds)")
else()
    set(CCACHE_STATUS "not found")
endif()

# Detect CPU core count for parallel build hints
cmake_host_system_information(RESULT CPU_CORES QUERY NUMBER_OF_LOGICAL_CORES)
if(CPU_CORES)
    set(PARALLEL_BUILD_HINT "-j${CPU_CORES}")
else()
    set(PARALLEL_BUILD_HINT "-j4")
    set(CPU_CORES "unknown")
endif()

message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "             Build Configuration Summary")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "  Platform:  ${PLATFORM_NAME} (${COMPILER_NAME} ${COMPILER_VERSION})")
message(STATUS "  Generator: ${CMAKE_GENERATOR}")
message(STATUS "  Build:     ${CMAKE_BUILD_TYPE}")
message(STATUS "───────────────────────────────────────────────────────")
message(STATUS "  Qt:        ${Qt6Core_VERSION}   ✓ (requires ${QT_MIN_VERSION}+)")
# Show Botan version with update status if available
if(BOTAN_LATEST_VERSION AND BOTAN_LATEST_VERSION VERSION_GREATER BOTAN_VERSION)
    message(STATUS "  Botan:     ${BOTAN_VERSION}   ⚠ (${BOTAN_LATEST_VERSION} available)")
elseif(BOTAN_LATEST_VERSION VERSION_EQUAL BOTAN_VERSION)
    message(STATUS "  Botan:     ${BOTAN_VERSION}   ✓ (up to date)")
else()
    message(STATUS "  Botan:     ${BOTAN_VERSION}   ✓")
endif()
message(STATUS "  Python:    ${PYTHON_VERSION_STR}   ${PYTHON_UPDATE_STATUS} (Botan build)")
message(STATUS "  CMake:     ${CMAKE_VERSION}  ✓ (requires ${CMAKE_MINIMUM_REQUIRED_VERSION}+)")
message(STATUS "───────────────────────────────────────────────────────")
message(STATUS "  XboxInternals: ${LIBRARY_TYPES_STR}")
message(STATUS "  Output:        ${CMAKE_BINARY_DIR}")
message(STATUS "  Targets:       VelocityNext, XboxInternals")
message(STATUS "───────────────────────────────────────────────────────")
message(STATUS "  Ccache:        ${CCACHE_STATUS}")
message(STATUS "  CPU Cores:     ${CPU_CORES} (use ${PARALLEL_BUILD_HINT} for parallel builds)")
message(STATUS "═══════════════════════════════════════════════════════")

# Calculate and display configuration time
string(TIMESTAMP CONFIG_END_TIME "%s")
math(EXPR CONFIG_DURATION "${CONFIG_END_TIME} - ${CONFIG_START_TIME}")
message(STATUS "Configuration completed in ${CONFIG_DURATION} seconds")
message(STATUS "")

# Display helpful next steps
message(STATUS "Next steps:")
# Detect preset name from binary directory path
string(REGEX MATCH "out/build/([^/]+)$" _ "${CMAKE_BINARY_DIR}")
if(CMAKE_MATCH_1)
    message(STATUS "  Build:   cmake --build --preset ${CMAKE_MATCH_1}")
else()
    message(STATUS "  Build:   cmake --build ${CMAKE_BINARY_DIR}")
endif()
if(WIN32)
    message(STATUS "  Run:     ${CMAKE_BINARY_DIR}/VelocityNext/VelocityNext.exe")
elseif(APPLE)
    message(STATUS "  Run:     ${CMAKE_BINARY_DIR}/VelocityNext/VelocityNext.app/Contents/MacOS/VelocityNext")
else()
    message(STATUS "  Run:     ${CMAKE_BINARY_DIR}/VelocityNext/VelocityNext")
endif()
message(STATUS "")

# ------------------------------
# Subdirectories
# ------------------------------
add_subdirectory(DeviceHelper)
add_subdirectory(XboxInternals)
add_subdirectory(Velocity VelocityNext)