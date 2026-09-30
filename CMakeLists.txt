cmake_minimum_required(VERSION 3.21)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
project(Videx VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg Concurrent OpenGLWidgets)
include(cmake/ConfigureImageLibraries.cmake)
include(cmake/ConfigureMpv.cmake)
set(VIDEX_SOURCES
    src/media/MediaType.cpp src/media/MediaType.h
    src/media/PlaybackTime.cpp src/media/PlaybackTime.h
    src/media/MpvPlayer.cpp src/media/MpvPlayer.h
    src/media/MediaNavigation.cpp src/media/MediaNavigation.h
    src/ui/player/VideoView.cpp src/ui/player/VideoView.h
    src/media/ImageDecoder.cpp src/media/ImageDecoder.h
    src/media/HeifDecoder.cpp src/media/HeifDecoder.h
    src/media/ImageLoader.cpp src/media/ImageLoader.h
    src/ui/images/ImageView.cpp src/ui/images/ImageView.h
    src/ui/images/ImagePane.cpp src/ui/images/ImagePane.h
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
target_link_libraries(videx_ui PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets
    PRIVATE Qt6::Svg Qt6::Concurrent Exiv2::exiv2lib VidexRaw heif VidexMpv
    PUBLIC Qt6::OpenGLWidgets)
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
option(VIDEX_BUILD_BENCHMARKS "Build local RAW and media performance checks" OFF)
if(VIDEX_BUILD_BENCHMARKS)
    add_executable(videx_raw_benchmark tools/RawBenchmark.cpp)
    target_include_directories(videx_raw_benchmark PRIVATE src)
    target_link_libraries(videx_raw_benchmark PRIVATE Qt6::Core VidexRaw)
    add_executable(videx_performance_checks tools/PerformanceChecks.cpp)
    target_link_libraries(videx_performance_checks PRIVATE videx_ui)
endif()
if(BUILD_TESTING)
    # Recompile playback connections with assertions; reuse the remaining UI
    # and its generated meta-objects without generating duplicate ones here.
    add_executable(videx_playback_lifecycle_checks tests/PlaybackLifecycleChecks.cpp
        src/media/MpvPlayer.cpp src/ui/player/VideoView.cpp
        src/ui/player/PlayerWidget.cpp)
    set_target_properties(videx_playback_lifecycle_checks PROPERTIES AUTOMOC OFF)
    target_link_libraries(videx_playback_lifecycle_checks PRIVATE videx_ui VidexMpv)
    target_compile_definitions(videx_playback_lifecycle_checks PRIVATE QT_FORCE_ASSERTS)
    if(MSVC)
        target_compile_options(videx_playback_lifecycle_checks PRIVATE /utf-8 /W4)
    else()
        target_compile_options(videx_playback_lifecycle_checks PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME playback_lifecycle_checks COMMAND videx_playback_lifecycle_checks -platform offscreen)
    set_tests_properties(playback_lifecycle_checks PROPERTIES TIMEOUT 60)
    add_executable(videx_playback_checks tests/PlaybackChecks.cpp)
    target_link_libraries(videx_playback_checks PRIVATE videx_ui)
    add_test(NAME playback_checks COMMAND videx_playback_checks -platform offscreen)
    set_tests_properties(playback_checks PROPERTIES TIMEOUT 120)
    add_executable(videx_image_checks tests/ImageChecks.cpp)
    target_link_libraries(videx_image_checks PRIVATE videx_ui Exiv2::exiv2lib)
    target_compile_definitions(videx_image_checks PRIVATE VIDEX_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    FetchContent_GetProperties(libheif)
    if(libheif_POPULATED)
        target_compile_definitions(videx_image_checks PRIVATE VIDEX_HEIF_TEST_FILES_DIR="${libheif_SOURCE_DIR}/examples")
    endif()
    add_test(NAME image_checks COMMAND videx_image_checks -platform offscreen)
    set_tests_properties(image_checks PROPERTIES TIMEOUT 180)
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
include(cmake/QtImageFormats.cmake)
include(cmake/ImageRuntime.cmake)
if(WIN32)
    foreach(executable Videx videx_image_checks videx_ui_checks videx_playback_checks videx_playback_lifecycle_checks videx_performance_checks)
        if(TARGET ${executable})
            add_custom_command(TARGET ${executable} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MPV_RUNTIME}" "$<TARGET_FILE_DIR:${executable}>")
        endif()
    endforeach()
    install(FILES "${MPV_RUNTIME}" DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS Videx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY docs/licenses DESTINATION ${CMAKE_INSTALL_DATADIR}/videx)
install(FILES docs/third-party.md DESTINATION ${CMAKE_INSTALL_DATADIR}/videx)
