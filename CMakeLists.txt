cmake_minimum_required(VERSION 3.21)
project(Videx VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
set(VIDEX_SOURCES
    src/app/ThemeManager.cpp src/app/ThemeManager.h
    src/app/BrowserController.cpp src/app/BrowserController.h src/app/UiState.h
    src/services/SettingsService.cpp src/services/SettingsService.h
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/common/UiComponents.cpp src/ui/common/UiComponents.h
    src/ui/common/ComboBox.cpp src/ui/common/ComboBox.h
    src/ui/explorer/ExplorerWidget.cpp src/ui/explorer/ExplorerWidget.h
    src/ui/player/PlayerWidget.cpp src/ui/player/PlayerWidget.h
    src/ui/player/PlayerControls.cpp src/ui/player/PlayerControls.h
    src/ui/dialogs/SettingsDialog.cpp src/ui/dialogs/SettingsDialog.h
    src/ui/dialogs/AboutDialog.cpp src/ui/dialogs/AboutDialog.h
    src/ui/dialogs/OpenPathDialog.cpp src/ui/dialogs/OpenPathDialog.h
    resources/resources.qrc)
add_library(videx_ui STATIC ${VIDEX_SOURCES})
target_include_directories(videx_ui PUBLIC src)
target_link_libraries(videx_ui PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
if(MSVC)
    target_compile_options(videx_ui PUBLIC /utf-8 PRIVATE /W4)
else()
    target_compile_options(videx_ui PRIVATE -Wall -Wextra -Wpedantic)
endif()
qt_add_executable(Videx WIN32 src/main.cpp)
if(WIN32)
    enable_language(RC)
    configure_file(resources/windows/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/videx.rc @ONLY)
    target_sources(Videx PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/videx.rc)
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/videx.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/images/logo.ico")
endif()
target_link_libraries(Videx PRIVATE videx_ui)
include(CTest)
if(BUILD_TESTING)
    add_executable(videx_ui_checks tests/UiChecks.cpp)
    target_link_libraries(videx_ui_checks PRIVATE videx_ui)
    add_test(NAME ui_checks COMMAND videx_ui_checks -platform offscreen)
    set_tests_properties(ui_checks PROPERTIES TIMEOUT 60)
    if(TARGET Qt6::QOffscreenIntegrationPlugin)
        set_tests_properties(ui_checks PROPERTIES ENVIRONMENT
            "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
    endif()
endif()
include(GNUInstallDirs)
install(TARGETS Videx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
