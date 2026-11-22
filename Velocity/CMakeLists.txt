# TextEncoding module sources
set(TEXT_ENCODING_SOURCES
  TextEncoding/TextDecoder.cpp
  TextEncoding/EncodingDetector.cpp
  TextEncoding/ShiftJIS/CP932Decoder.cpp
  TextEncoding/ShiftJIS/CP932Mapping.cpp
  TextEncoding/GBK/CP936Decoder.cpp
  TextEncoding/GBK/CP936Mapping.cpp
  TextEncoding/Big5/CP950Decoder.cpp
  TextEncoding/Big5/CP950Mapping.cpp
  TextEncoding/EUCKR/CP949Decoder.cpp
  TextEncoding/EUCKR/CP949Mapping.cpp
)

set(SOURCES
  about.cpp
  achievementcreationwizard.cpp
  addressconverterdialog.cpp
  AvatarAssetDownloader.cpp
  boxartretriever.cpp
  certificatedialog.cpp
  clustertooldialog.cpp
  contentnames.cpp
  creationwizard.cpp
  deviceviewer.cpp
  dragdroptreewidget.cpp
  fatxfiledialog.cpp
  fatxpathgendialog.cpp
  flashdriveconfigdatadialog.cpp
  gameadderdialog.cpp
  gamerpicturepackdialog.cpp
  githubcommitsdialog.cpp
  gpddownloader.cpp
  gpduploader.cpp
  imagedialog.cpp
  licensingdatadialog.cpp
  main.cpp
  mainwindow.cpp
  metadata.cpp
  multiprogressdialog.cpp
  nightcharts.cpp
  packageviewer.cpp
  partitiondialog.cpp
  preferencesdialog.cpp
  profilecleanerwizard.cpp
  profilecreatorwizard.cpp
  profileeditor.cpp
  propertiesdialog.cpp
  qthelpers.cpp
  renamedialog.cpp
  securitysectordialog.cpp
  singleprogressdialog.cpp
  stfstoolsdialog.cpp
  stfsvolumedescriptordialog.cpp
  strbdialog.cpp
  svoddialog.cpp
  svodfileinfodialog.cpp
  streambasedevice.cpp
  svodtooldialog.cpp
  videoplayback.cpp
  isodialog.cpp
  isosectordialog.cpp
  xexdialog.cpp
  xuizdialog.cpp
  fatxdevicenotifier.cpp
  profileselectiondialog.cpp
  devicecontentviewer.cpp
  devicehelperclient.cpp
  svodvolumedescriptordialog.cpp
  stfsvolumedescriptordialog.cpp
  themecreationwizard.cpp
  textdialog.cpp
  thememanager.cpp
  titlenameservice.cpp
  titleidfinder.cpp
  titleidfinderdialog.cpp
  transferflagsdialog.cpp
  xdbfdialog.cpp
  xmldialog.cpp
  zipviewer.cpp
  ytgrdialog.cpp
  ${TEXT_ENCODING_SOURCES}
)

set(UI_FORMS
  mainwindow.ui
  packageviewer.ui
  profileeditor.ui
  about.ui
  metadata.ui
  certificatedialog.ui
  licensingdatadialog.ui
  transferflagsdialog.ui
  xdbfdialog.ui
  strbdialog.ui
  imagedialog.ui
  renamedialog.ui
  creationwizard.ui
  achievementcreationwizard.ui
  propertiesdialog.ui
  addressconverterdialog.ui
  stfstoolsdialog.ui
  singleprogressdialog.ui
  profilecreatorwizard.ui
  themecreationwizard.ui
  gameadderdialog.ui
  titleidfinderdialog.ui
  gamerpicturepackdialog.ui
  preferencesdialog.ui
  githubcommitsdialog.ui
  fatxpathgendialog.ui
  profilecleanerwizard.ui
  svoddialog.ui
  svodfileinfodialog.ui
  svodtooldialog.ui
  svodvolumedescriptordialog.ui
  isosectordialog.ui
  xexdialog.ui
  xuizdialog.ui
  profileselectiondialog.ui
  devicecontentviewer.ui
  stfsvolumedescriptordialog.ui
  multiprogressdialog.ui
  ytgrdialog.ui
  deviceviewer.ui
  clustertooldialog.ui
  fatxfiledialog.ui
  partitiondialog.ui
  securitysectordialog.ui
  flashdriveconfigdatadialog.ui
  xmldialog.ui
  textdialog.ui
  zipviewer.ui
)

# Platform-specific executable properties
if(APPLE)
    # macOS bundle configuration
    set(MACOSX_BUNDLE_GUI_IDENTIFIER com.velocitynext.app)
    set(MACOSX_BUNDLE_BUNDLE_NAME VelocityNext)
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_ICON_FILE velocity.icns)
    
    add_executable(VelocityNext MACOSX_BUNDLE ${SOURCES} ${UI_FORMS})
    
else()
    # Windows/Linux executable
    add_executable(VelocityNext ${SOURCES} ${UI_FORMS})
    
    if(WIN32)
        set_target_properties(VelocityNext PROPERTIES WIN32_EXECUTABLE TRUE)
        target_sources(VelocityNext PRIVATE velocity.rc)
    endif()
endif()

set_target_properties(VelocityNext PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    # moc/uic only need Qt headers, not the linked XboxInternals binary.
    AUTOGEN_ORIGIN_DEPENDS OFF
)

# Resources compile in their own object library so rcc does not wait for
# XboxInternals (and the single-TU Botan build behind it) to link.
add_library(velocity_resources OBJECT Resources.qrc)
set_target_properties(velocity_resources PROPERTIES AUTORCC ON FOLDER "Applications")
target_link_libraries(velocity_resources PRIVATE Qt6::Core)
target_sources(VelocityNext PRIVATE $<TARGET_OBJECTS:velocity_resources>)

option(VELOCITY_PCH "Precompile common Qt headers for the GUI" ON)
if(VELOCITY_PCH)
  target_precompile_headers(VelocityNext PRIVATE
    <QtWidgets/QtWidgets>
    <QtNetwork/QNetworkAccessManager>
    <QtNetwork/QNetworkReply>
  )
endif()

# Common include directories for all platforms
target_include_directories(VelocityNext
  PRIVATE
    # Current directory for header files co-located with source
    ${CMAKE_CURRENT_LIST_DIR}
    # Qt private headers for advanced features
    ${Qt6Core_PRIVATE_INCLUDE_DIRS}
)

# Common linking for all platforms - ALWAYS use our BotanAmalgamation
# zipviewer.cpp uses qzipreader_p.h. Qt 6.10+ only exposes the private include
# directories through the CorePrivate module, which is optional for older Qt.
if(Qt6CorePrivate_FOUND AND TARGET Qt6::CorePrivate)
  target_link_libraries(VelocityNext PRIVATE Qt6::CorePrivate)
endif()

target_link_libraries(VelocityNext
  PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Xml
    Qt6::Widgets  
    Qt6::Network
    Qt6::Concurrent
    XboxInternals::XboxInternals  # Use namespaced target (modern CMake)
    BotanAmalgamation  # Our custom amalgamation on ALL platforms
    device_helper_protocol  # Portable helper IPC framing
    velocity_compiler_flags  # Inherit common compile options
)

# Optional video playback. AUTO enables it when Qt Multimedia is present, ON
# requires it, OFF builds the stub that reports missing support.
set(VELOCITY_VIDEO "AUTO" CACHE STRING "Video playback support: AUTO, ON or OFF")
set_property(CACHE VELOCITY_VIDEO PROPERTY STRINGS AUTO ON OFF)
if(NOT VELOCITY_VIDEO MATCHES "^(AUTO|ON|OFF)$")
  message(FATAL_ERROR "VELOCITY_VIDEO must be AUTO, ON or OFF (got '${VELOCITY_VIDEO}')")
endif()
if(NOT VELOCITY_VIDEO STREQUAL "OFF")
  find_package(Qt6 ${QT_MIN_VERSION} QUIET COMPONENTS Multimedia MultimediaWidgets)
endif()
if(NOT VELOCITY_VIDEO STREQUAL "OFF" AND TARGET Qt6::MultimediaWidgets)
  target_sources(VelocityNext PRIVATE videodialog.cpp)
  target_link_libraries(VelocityNext PRIVATE Qt6::Multimedia Qt6::MultimediaWidgets)
  target_compile_definitions(VelocityNext PRIVATE VELOCITY_HAS_VIDEO)
  message(STATUS "Video playback: enabled (Qt Multimedia)")
elseif(VELOCITY_VIDEO STREQUAL "ON")
  message(FATAL_ERROR "VELOCITY_VIDEO=ON but Qt6 Multimedia/MultimediaWidgets were not found")
else()
  message(STATUS "Video playback: disabled")
endif()

# Define VERSION and platform build name
target_compile_definitions(VelocityNext 
  PRIVATE 
    VERSION="${PROJECT_VERSION}"
    PLATFORM_BUILD_NAME="${PLATFORM_BUILD_NAME}"
)

# App requires C++20
target_compile_features(VelocityNext PUBLIC cxx_std_20)

# Organize in the IDE tree (Qt Creator, VS Code, ...)
set_target_properties(VelocityNext PROPERTIES FOLDER "Applications")

# Post-build: keep the device helper next to the GUI for portable deployment.
if(TARGET velocity-device-helper)
    add_dependencies(VelocityNext velocity-device-helper)
    add_custom_command(TARGET VelocityNext POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:velocity-device-helper>
            $<TARGET_FILE_DIR:VelocityNext>
        COMMENT "Copying velocity-device-helper to Velocity output directory"
    )
endif()

# Post-build: Copy XboxInternals DLL to Velocity output directory (only when building shared library)
if(BUILD_XBOXINTERNALS_SHARED)
    add_custom_command(TARGET VelocityNext POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:XboxInternals::XboxInternals>
            $<TARGET_FILE_DIR:VelocityNext>
        COMMENT "Copying libXboxInternals.dll to Velocity output directory"
    )
endif()

# ------------------------------
# Install
# ------------------------------
include(GNUInstallDirs)
if(APPLE)
  # macdeployqt puts dependencies in Contents/Frameworks; also support a plain
  # prefix install where the library lives in <prefix>/lib.
  set_target_properties(VelocityNext PROPERTIES
    INSTALL_RPATH "@executable_path/../Frameworks;@executable_path/../../lib")
elseif(UNIX)
  set_target_properties(VelocityNext PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

install(TARGETS VelocityNext
  BUNDLE  DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(APPLE)
  install(TARGETS velocity-device-helper RUNTIME DESTINATION VelocityNext.app/Contents/MacOS)
else()
  install(TARGETS velocity-device-helper RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(FILES ${PROJECT_SOURCE_DIR}/COPYING ${PROJECT_SOURCE_DIR}/NOTICE
  DESTINATION ${CMAKE_INSTALL_DOCDIR})
