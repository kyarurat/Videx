set(CMAKE_AUTOMOC OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_AUTORCC OFF)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)

find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1 GIT_SHALLOW TRUE GIT_SUBMODULES "")
    FetchContent_MakeAvailable(zlib)
    set_target_properties(zlibstatic PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
    set(ZLIB_INCLUDE_DIRS "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}")
    set(ZLIB_LIBRARIES ZLIB::ZLIB)
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/modules")
endif()

if(NOT TARGET Exiv2::exiv2lib)
    find_package(EXPAT QUIET)
    if(NOT EXPAT_FOUND)
        foreach(option BUILD_TOOLS BUILD_EXAMPLES BUILD_TESTS BUILD_DOCS BUILD_FUZZERS BUILD_PKGCONFIG ENABLE_INSTALL SHARED_LIBS)
            set(EXPAT_${option} OFF CACHE BOOL "" FORCE)
        endforeach()
        FetchContent_Declare(videx_expat
            URL https://codeload.github.com/libexpat/libexpat/tar.gz/refs/tags/R_2_8_5
            URL_HASH SHA256=fd022c541a189bd5bee042a22188351b31e39b9389c2525fa98c28bd05c9ef21
            SOURCE_SUBDIR expat TLS_VERIFY ON)
        FetchContent_MakeAvailable(videx_expat)
        set_target_properties(expat PROPERTIES POSITION_INDEPENDENT_CODE ON)
        add_library(EXPAT::EXPAT ALIAS expat)
        list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/expat-module")
    endif()
    set(BUILD_SHARED_LIBS ON)
    set(EXIV2_BUILD_EXIV2_COMMAND OFF CACHE BOOL "" FORCE)
    set(EXIV2_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(EXIV2_ENABLE_XMP ON CACHE BOOL "" FORCE)
    set(EXIV2_ENABLE_BROTLI OFF CACHE BOOL "" FORCE)
    set(EXIV2_ENABLE_INIH OFF CACHE BOOL "" FORCE)
    set(EXIV2_ENABLE_VIDEO OFF CACHE BOOL "" FORCE)
    set(EXIV2_ENABLE_WEBREADY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(exiv2
        URL https://codeload.github.com/Exiv2/exiv2/tar.gz/refs/tags/v0.28.9
        URL_HASH SHA256=700b76b97695b2fab4ef8c79619c68ae57d09e0c130724791cafbd39e0eb4aef
        TLS_VERIFY ON)
    FetchContent_MakeAvailable(exiv2)
    # The bundled SDK passes a uint32_t to %lX on Windows (unsigned int).
    # Preserve the format and explicitly supply the required unsigned long.
    set(xmp_meta_source "${exiv2_SOURCE_DIR}/xmpsdk/src/XMPMeta.cpp")
    file(READ "${xmp_meta_source}" xmp_meta_text)
    string(REPLACE "\"%lX\", (num)" "\"%lX\", static_cast<unsigned long>(num)"
        xmp_meta_fixed "${xmp_meta_text}")
    if(NOT xmp_meta_fixed STREQUAL xmp_meta_text)
        file(WRITE "${xmp_meta_source}" "${xmp_meta_fixed}")
    endif()
    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GetProcAddress returns FARPROC; Exiv2 casts it to the matching WinAPI
        # ReplaceFileA signature. Keep this exception local to that source.
        set_property(SOURCE "${exiv2_SOURCE_DIR}/src/basicio.cpp"
            TARGET_DIRECTORY exiv2lib APPEND PROPERTY COMPILE_OPTIONS -Wno-cast-function-type)
    endif()
    target_include_directories(exiv2lib SYSTEM PUBLIC "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>")
    set(BUILD_SHARED_LIBS OFF)
endif()

if(NOT TARGET PkgConfig::SYSTEM_RAW)
    FetchContent_Declare(libraw
        URL https://codeload.github.com/LibRaw/LibRaw/tar.gz/refs/tags/0.22.2
        URL_HASH SHA256=627928088300ecde6ca91ffd202e189203f04ad61ad12f0fe9dc57b9a7a0fb3c
        TLS_VERIFY ON)
    FetchContent_MakeAvailable(libraw)
    # Upstream redefines its internal ZERO macro in this translation unit.
    # Explicitly end the previous definition, preserving the decoder's version.
    set(raw_jpeg_source "${libraw_SOURCE_DIR}/src/decompressors/losslessjpeg.cpp")
    file(READ "${raw_jpeg_source}" raw_jpeg_text)
    if(NOT raw_jpeg_text MATCHES "#undef ZERO")
        string(REPLACE "#define ZERO(a) do" "#undef ZERO\n#define ZERO(a) do" raw_jpeg_text "${raw_jpeg_text}")
        file(WRITE "${raw_jpeg_source}" "${raw_jpeg_text}")
    endif()
    # LibRaw ships no CMake project. Build its documented library source set.
    file(GLOB_RECURSE raw_sources CONFIGURE_DEPENDS "${libraw_SOURCE_DIR}/src/*.cpp")
    add_library(videx_raw STATIC ${raw_sources})
    option(VIDEX_RAW_OPENMP "Use OpenMP CPU parallelism for bundled LibRaw" ON)
    option(VIDEX_RAW_OPTIMIZE_DEBUG "Optimize bundled LibRaw in GNU/Clang Debug builds" ON)
    if(VIDEX_RAW_OPTIMIZE_DEBUG AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
        target_compile_options(videx_raw PRIVATE "$<$<CONFIG:Debug>:-O2>")
    endif()
    if(VIDEX_RAW_OPENMP)
        find_package(OpenMP QUIET COMPONENTS CXX)
        if(OpenMP_CXX_FOUND)
            target_link_libraries(videx_raw PUBLIC OpenMP::OpenMP_CXX)
            target_compile_definitions(videx_raw PUBLIC LIBRAW_FORCE_OPENMP)
            set_property(TARGET videx_raw PROPERTY VIDEX_USES_OPENMP TRUE)
            message(STATUS "Videx LibRaw: OpenMP enabled for all build configurations")
        else()
            message(WARNING "VIDEX_RAW_OPENMP is ON but OpenMP was not found; RAW decoding will be serial")
        endif()
    endif()
    target_include_directories(videx_raw SYSTEM PUBLIC "${libraw_SOURCE_DIR}")
    target_compile_definitions(videx_raw PUBLIC LIBRAW_NODLL PRIVATE USE_ZLIB USE_X3FTOOLS USE_6BY9RPI)
    target_link_libraries(videx_raw PRIVATE ZLIB::ZLIB)
    if(WIN32)
        target_compile_definitions(videx_raw PUBLIC LIBRAW_WIN32_UNICODEPATHS)
        target_compile_definitions(videx_raw PRIVATE _USE_MATH_DEFINES)
        target_link_libraries(videx_raw PRIVATE ws2_32)
    endif()
endif()
