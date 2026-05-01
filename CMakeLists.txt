cmake_minimum_required(VERSION 3.22)
project(framelit VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6 — prefer the standard ~/Qt install location on macOS
set(QT_DEFAULT_MAJOR_VERSION 6)

if(APPLE)
list(PREPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.11.0/macos")
elseif(LINUX)
list(PREPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.11.0/gcc_64")
elseif(WIN32)
if(MSVC)
    list(PREPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.0/msvc2022_64")
else()
    list(PREPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.0/mingw_64")
endif()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets OpenGL OpenGLWidgets Test)

set(SOURCES
    src/main.cpp
    src/appcontroller.cpp
    src/recorderworker.cpp
    src/recordingstrategy.hpp
    src/bufferedstrategy.cpp
    src/streamingstrategy.cpp
    src/capture/screencaptureworker.cpp
    src/capture/framestore.cpp
    src/encoding/gifencoder.cpp
    src/encoding/videoencoder.cpp
    src/ui/capturewindow.cpp
    src/ui/controlbar.cpp
    src/platform/screencapturebackend.hpp
)

if(APPLE)
    list(APPEND SOURCES
        src/platform/macos_window.mm
        src/platform/sckbackend.mm
        src/globalinputmanager.mm
    )
else()
    list(APPEND SOURCES
        src/platform/qtscreenbackend.cpp
    )
endif()

if(UNIX AND NOT APPLE)
    list(APPEND SOURCES src/platform/x11_window.cpp)
endif()

set(HEADERS
    src/appcontroller.hpp
    src/recorderworker.hpp
    src/capture/screencaptureworker.hpp
    src/capture/framestore.hpp
    src/encoding/gifencoder.hpp
    src/platform/screencapturebackend.hpp
    src/ui/capturewindow.hpp
    src/ui/controlbar.hpp
)

if(APPLE)
    list(APPEND HEADERS
        src/platform/sckbackend.hpp
        src/platform/macos_window.h
        src/globalinputmanager.hpp
    )
else()
    list(APPEND HEADERS src/platform/qtscreenbackend.hpp)
endif()

if(UNIX AND NOT APPLE)
    list(APPEND HEADERS src/platform/x11_window.hpp)
endif()

qt_add_executable(framelit
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(framelit PRIVATE src)

# ---------------------------------------------------------------------------
# giflib (Homebrew preferred; FetchContent fallback)
# ---------------------------------------------------------------------------
find_library(GIFLIB_LIB gif HINTS /opt/homebrew/opt/giflib/lib)
find_path(GIFLIB_INCLUDE_DIR gif_lib.h HINTS /opt/homebrew/opt/giflib/include)

if(GIFLIB_LIB AND GIFLIB_INCLUDE_DIR)
    message(STATUS "Found giflib: ${GIFLIB_LIB}")
else()
    message(STATUS "giflib not found via find_library — fetching via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        giflib
        URL      https://sourceforge.net/projects/giflib/files/giflib-5.2.2.tar.gz
        URL_HASH MD5=913dd251492134e235ee3c9a91987a4d
    )
    FetchContent_GetProperties(giflib)
    if(NOT giflib_POPULATED)
        FetchContent_Populate(giflib)
        # giflib has no CMakeLists; build the single lib target manually
        file(GLOB GIFLIB_SRCS "${giflib_SOURCE_DIR}/*.c")
        list(FILTER GIFLIB_SRCS EXCLUDE REGEX "(giftext|giftool|gifecho|gifcp|gifclrmp|giffix|gifinfo|gifsplit|gifjoin|gifinto|gifundx)\\.c$")
        add_library(gif STATIC ${GIFLIB_SRCS})
        target_include_directories(gif PUBLIC "${giflib_SOURCE_DIR}")
    endif()
    set(GIFLIB_LIB gif)
    set(GIFLIB_INCLUDE_DIR "${giflib_SOURCE_DIR}")
endif()

target_include_directories(framelit PRIVATE ${GIFLIB_INCLUDE_DIR})

target_link_libraries(framelit PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    ${GIFLIB_LIB}
)

if(APPLE)
    target_link_libraries(framelit PRIVATE
        "-framework AppKit"
        "-framework ScreenCaptureKit"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework CoreGraphics"
    )
endif()

if(UNIX AND NOT APPLE)
    # XShape extension (libXext) for real click-through on X11
    target_link_libraries(framelit PRIVATE Xext X11)
endif()

# Per-platform output name: Framelit on macOS/Windows, framelit on Linux.
if(APPLE OR WIN32)
    set_target_properties(framelit PROPERTIES OUTPUT_NAME "Framelit")
else()
    set_target_properties(framelit PROPERTIES OUTPUT_NAME "framelit")
endif()

# macOS bundle settings
if(APPLE)
    set_target_properties(framelit PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.sc.framelit"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
    # Ad-hoc sign with screen recording entitlement so TCC grants work.
    add_custom_command(TARGET framelit POST_BUILD
        COMMAND codesign --force --deep
                --sign "ED7BA643A218B65A4A1068D40A77E73EFA30ED56"
                --entitlements "${CMAKE_SOURCE_DIR}/entitlements.plist"
                "$<TARGET_BUNDLE_DIR:framelit>"
        COMMENT "Signing with Developer ID"
    )
endif()

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------
enable_testing()

qt_add_executable(capture_region_test
    tests/capture_region_test.cpp
)

target_include_directories(capture_region_test PRIVATE src)

target_link_libraries(capture_region_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME CaptureRegionTest COMMAND capture_region_test)

qt_add_executable(recording_settings_test
    tests/recording_settings_test.cpp
)

target_include_directories(recording_settings_test PRIVATE src)

target_link_libraries(recording_settings_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME RecordingSettingsTest COMMAND recording_settings_test)

qt_add_executable(mouse_panner_test
    tests/mouse_panner_test.cpp
)

target_include_directories(mouse_panner_test PRIVATE src)

target_link_libraries(mouse_panner_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME MousePannerTest COMMAND mouse_panner_test)
