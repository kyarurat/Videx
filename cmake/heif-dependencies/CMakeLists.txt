set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(ENABLE_SDL OFF)
set(ENABLE_DECODER OFF)
set(ENABLE_ENCODER OFF)
FetchContent_Declare(de265
    URL https://codeload.github.com/strukturag/libde265/tar.gz/refs/tags/v1.0.19
    URL_HASH SHA256=b793af552a95d0c4624b46ecbb60359c1d62da77b6072a86d9ec0cc33cc56cd6
    TLS_VERIFY ON
    SOURCE_SUBDIR videx-download-only)
FetchContent_MakeAvailable(de265)
# Both upstream projects define an unconditional "dist" utility target. Scope
# libde265's target name without changing its decoder or public API.
file(READ "${de265_SOURCE_DIR}/CMakeLists.txt" de265_cmake)
string(REPLACE "add_custom_target(dist" "add_custom_target(videx_de265_dist" de265_scoped "${de265_cmake}")
string(REPLACE "add_definitions(-Wall -Werror=return-type -Werror=unused-result -Werror=reorder)"
    "add_compile_options(-Wall -Werror=return-type -Werror=unused-result \"$<$<COMPILE_LANGUAGE:CXX>:-Werror=reorder>\")"
    de265_scoped "${de265_scoped}")
if(NOT de265_cmake STREQUAL de265_scoped)
    file(WRITE "${de265_SOURCE_DIR}/CMakeLists.txt" "${de265_scoped}")
endif()
add_subdirectory("${de265_SOURCE_DIR}" "${de265_BINARY_DIR}")
target_compile_definitions(de265 INTERFACE LIBDE265_STATIC_BUILD)
target_include_directories(de265 INTERFACE "$<BUILD_INTERFACE:${de265_SOURCE_DIR}>")

set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTDATA OFF CACHE BOOL "" FORCE)
set(ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
set(ENABLE_DOCS OFF CACHE BOOL "" FORCE)
set(CONFIG_AV1_ENCODER 0 CACHE STRING "" FORCE)
set(AOM_TARGET_CPU generic CACHE STRING "Portable decoder without an assembler dependency")
if(WIN32 AND NOT PERL_EXECUTABLE)
    find_package(Git QUIET)
    get_filename_component(videx_git_directory "${GIT_EXECUTABLE}" DIRECTORY)
    find_program(PERL_EXECUTABLE perl HINTS "${videx_git_directory}/../usr/bin")
endif()
FetchContent_Declare(aom
    URL https://codeload.github.com/arthenica/libaom/tar.gz/refs/tags/v3.13.3
    URL_HASH SHA256=8f7de8fd795f33e9bd9d837108b11eaa33072da074fc4cdf5b3db07304d44e46
    TLS_VERIFY ON)
FetchContent_MakeAvailable(aom)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/modules")
set(BUILD_SHARED_LIBS ON)
set(WITH_LIBDE265 ON)
set(WITH_AOM_DECODER ON)
set(WITH_LIBDE265_PLUGIN OFF)
set(WITH_AOM_DECODER_PLUGIN OFF)
set(WITH_AOM_ENCODER OFF)
set(WITH_X265 OFF)
set(WITH_X264 OFF)
set(WITH_OpenH264_DECODER OFF)
set(WITH_LIBSHARPYUV OFF)
set(WITH_EXAMPLES OFF)
set(WITH_GDK_PIXBUF OFF)
set(BUILD_DOCUMENTATION OFF)
set(ENABLE_PLUGIN_LOADING OFF)
FetchContent_Declare(libheif
    URL https://codeload.github.com/strukturag/libheif/tar.gz/refs/tags/v1.21.2
    URL_HASH SHA256=79996de959d28ca82ef070c382304683f5cdaf04cbe2953a74587160a3710a36
    TLS_VERIFY ON)
FetchContent_MakeAvailable(libheif)
# Export macros belong to the DLL itself, not to consumers of its C API.
get_target_property(heif_interface_definitions heif INTERFACE_COMPILE_DEFINITIONS)
list(REMOVE_ITEM heif_interface_definitions LIBHEIF_EXPORTS HAVE_VISIBILITY)
set_property(TARGET heif PROPERTY INTERFACE_COMPILE_DEFINITIONS "${heif_interface_definitions}")
target_include_directories(heif SYSTEM PUBLIC
    "$<BUILD_INTERFACE:${libheif_SOURCE_DIR}/libheif/api>"
    "$<BUILD_INTERFACE:${libheif_BINARY_DIR}>")
