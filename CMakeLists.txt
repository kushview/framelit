cmake_minimum_required(VERSION 3.22)
project(ScreenCaptureQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6 — prefer the standard ~/Qt install location on macOS
set(QT_DEFAULT_MAJOR_VERSION 6)
list(PREPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.11.0/macos")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets OpenGL OpenGLWidgets Test)

set(SOURCES
    src/main.cpp
    src/appcontroller.cpp
    src/recorderworker.cpp
    src/capture/screencaptureworker.cpp
    src/ui/capturewindow.cpp
    src/ui/controlbar.cpp
)

set(HEADERS
    src/appcontroller.hpp
    src/recorderworker.hpp
    src/capture/screencaptureworker.hpp
    src/ui/capturewindow.hpp
    src/ui/controlbar.hpp
)

qt_add_executable(screen-capture-qt
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(screen-capture-qt PRIVATE src)

target_link_libraries(screen-capture-qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)

# macOS bundle settings
if(APPLE)
    set_target_properties(screen-capture-qt PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.sc.screen-capture-qt"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
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
