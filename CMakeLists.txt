cmake_minimum_required(VERSION 3.22)
project(framelit VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6 — prefer the standard ~/Qt install location on macOS
set(QT_DEFAULT_MAJOR_VERSION 6)

# CI-friendly Qt discovery: prefer paths exported by install-qt-action.
if(DEFINED ENV{Qt6_DIR} AND NOT DEFINED Qt6_DIR)
    set(Qt6_DIR "$ENV{Qt6_DIR}")
endif()
if(DEFINED ENV{Qt_ROOT_DIR})
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{Qt_ROOT_DIR}")
endif()

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
    src/ui/centerhandle.cpp
    src/ui/closebutton.cpp
    src/ui/controlbar.cpp
    src/ui/editwindow.cpp
    src/ui/mainmenu.cpp
    src/ui/systemtray.cpp
    src/ui/actions.cpp
    src/ui/preferencesdialog.cpp
    src/platform/screencapturebackend.hpp
)

if(APPLE)
    list(APPEND SOURCES
        src/platform/macos_window.mm
        src/platform/sckbackend.mm
        src/globalhotkeys.mm
    )
else()
    list(APPEND SOURCES
        src/globalhotkeys.cpp
        src/platform/qtscreenbackend.cpp
    )
endif()

if(UNIX AND NOT APPLE)
    list(APPEND SOURCES src/platform/x11_window.cpp)
endif()

# Common platform utilities
list(APPEND SOURCES src/platform/windowhelpers.cpp)

set(HEADERS
    src/appcontroller.hpp
    src/recorderworker.hpp
    src/capture/screencaptureworker.hpp
    src/capture/framestore.hpp
    src/encoding/gifencoder.hpp
    src/platform/screencapturebackend.hpp
    src/ui/capturewindow.hpp
    src/ui/centerhandle.hpp
    src/ui/closebutton.hpp
    src/ui/controlbar.hpp
    src/ui/editwindow.hpp
    src/ui/mainmenu.hpp
)

if(APPLE)
    list(APPEND HEADERS
        src/platform/sckbackend.hpp
        src/platform/macos_window.h
        src/globalhotkeys.hpp
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
        "-framework AVFoundation"
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
    option(FRAMELIT_RUN_MACDEPLOYQT "Run macdeployqt after build" ON)
    set(FRAMELIT_CODESIGN_IDENTITY "ED7BA643A218B65A4A1068D40A77E73EFA30ED56" CACHE STRING "Developer ID signing identity for Framelit.app")

    get_target_property(_qt_qmake_location Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake_location}" DIRECTORY)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin_dir}")

    # Generate AppIcon.icns from icon.png at configure time using sips + iconutil.
    set(_icon_src "${CMAKE_SOURCE_DIR}/icon.png")
    set(_iconset  "${CMAKE_BINARY_DIR}/AppIcon.iconset")
    set(_icns     "${CMAKE_BINARY_DIR}/AppIcon.icns")
    file(MAKE_DIRECTORY "${_iconset}")
    foreach(_sz 16 32 128 256 512)
        math(EXPR _sz2x "${_sz} * 2")
        execute_process(
            COMMAND sips -z ${_sz} ${_sz} "${_icon_src}" --out "${_iconset}/icon_${_sz}x${_sz}.png"
            OUTPUT_QUIET ERROR_QUIET)
        execute_process(
            COMMAND sips -z ${_sz2x} ${_sz2x} "${_icon_src}" --out "${_iconset}/icon_${_sz}x${_sz}@2x.png"
            OUTPUT_QUIET ERROR_QUIET)
    endforeach()
    execute_process(COMMAND iconutil -c icns "${_iconset}" -o "${_icns}")
    target_sources(framelit PRIVATE "${_icns}")
    set_source_files_properties("${_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    set_target_properties(framelit PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Framelit"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.sc.framelit"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE AppIcon
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist.in"
    )

    # Step 1: deploy Qt frameworks — signing done separately so we control the flags.
    if(FRAMELIT_RUN_MACDEPLOYQT)
        if(MACDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET framelit POST_BUILD
                COMMAND "${MACDEPLOYQT_EXECUTABLE}"
                        "$<TARGET_BUNDLE_DIR:framelit>"
                        -always-overwrite
                        -no-codesign
                COMMENT "Deploying Qt frameworks with macdeployqt"
            )
        else()
            message(WARNING "FRAMELIT_RUN_MACDEPLOYQT is ON but macdeployqt was not found. Skipping.")
        endif()
    endif()

    # Step 2: sign the bundle.
    # - Developer ID identity: hardened runtime + secure timestamp for notarization.
    # - Ad hoc identity (-): simple local/CI signing without notarization flags.
    if(FRAMELIT_CODESIGN_IDENTITY STREQUAL "-")
        add_custom_command(TARGET framelit POST_BUILD
            COMMAND codesign --force --deep
                    --sign "-"
                    "$<TARGET_BUNDLE_DIR:framelit>"
            COMMENT "Signing Framelit.app (ad hoc)"
        )
    else()
        add_custom_command(TARGET framelit POST_BUILD
            COMMAND codesign --force --deep
                    --sign "${FRAMELIT_CODESIGN_IDENTITY}"
                    --entitlements "${CMAKE_SOURCE_DIR}/entitlements.plist"
                    --options runtime
                    --timestamp
                    "$<TARGET_BUNDLE_DIR:framelit>"
            COMMENT "Signing Framelit.app (hardened runtime + timestamp)"
        )
    endif()

    # Notarize Example
    # echo "Notarizing"
    # xcrun notarytool submit \
    #     "deploy/${PKG_FILENAME}" \
    #     --keychain-profile "notarytool-profile" \
    #     --wait

    # echo "Stapling notarization ticket..."
    # xcrun stapler staple "deploy/${PKG_FILENAME}"
    # xcrun stapler validate "deploy/${PKG_FILENAME}"

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
