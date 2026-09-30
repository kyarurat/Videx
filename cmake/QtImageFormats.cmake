option(VIDEX_BUILD_IMAGE_PLUGINS "Build Qt's extra image plugins (WebP, TIFF, TGA, ICNS, WBMP)" ${WIN32})
if(VIDEX_BUILD_IMAGE_PLUGINS)
    include(FetchContent)
    include(ExternalProject)
    FetchContent_Declare(videx_qtimageformats
        GIT_REPOSITORY https://github.com/qt/qtimageformats.git
        GIT_TAG "v${Qt6_VERSION}" GIT_SHALLOW TRUE GIT_SUBMODULES ""
        SOURCE_SUBDIR videx-download-only)
    FetchContent_MakeAvailable(videx_qtimageformats)
    set(VIDEX_IMAGE_PLUGIN_PREFIX "${CMAKE_BINARY_DIR}/imageformats-runtime")
    get_filename_component(videx_cmake_bin "${CMAKE_COMMAND}" DIRECTORY)
    find_program(VIDEX_NINJA_EXECUTABLE NAMES ninja ninja-build
        HINTS "${videx_cmake_bin}/../../Ninja" REQUIRED)
    ExternalProject_Add(videx_image_plugins
        SOURCE_DIR "${videx_qtimageformats_SOURCE_DIR}"
        BINARY_DIR "${CMAKE_BINARY_DIR}/qtimageformats-ninja-build"
        CMAKE_GENERATOR Ninja
        CMAKE_ARGS
            "-DCMAKE_MAKE_PROGRAM=${VIDEX_NINJA_EXECUTABLE}"
            "-DQt6_DIR=${Qt6_DIR}"
            "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCMAKE_BUILD_TYPE=Release"
            "-DCMAKE_INSTALL_PREFIX=${VIDEX_IMAGE_PLUGIN_PREFIX}"
            -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
        INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release)
    foreach(executable Videx videx_image_checks videx_ui_checks videx_playback_checks)
        if(TARGET ${executable})
            add_dependencies(${executable} videx_image_plugins)
            add_custom_command(TARGET ${executable} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${VIDEX_IMAGE_PLUGIN_PREFIX}/plugins/imageformats"
                    "$<TARGET_FILE_DIR:${executable}>/imageformats")
        endif()
    endforeach()
    install(DIRECTORY "${VIDEX_IMAGE_PLUGIN_PREFIX}/plugins/imageformats"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
