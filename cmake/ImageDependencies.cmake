# System packages are preferred for distribution builds. The pinned fallback keeps
# a fresh desktop checkout buildable without manually installing C++ libraries.
include(FetchContent)
option(VIDEX_FETCH_IMAGE_DEPENDENCIES "Fetch missing image dependencies" ON)

find_package(exiv2 0.28 CONFIG QUIET)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SYSTEM_RAW QUIET IMPORTED_TARGET libraw>=0.22)
endif()

if(NOT TARGET Exiv2::exiv2lib OR NOT TARGET PkgConfig::SYSTEM_RAW)
    if(NOT VIDEX_FETCH_IMAGE_DEPENDENCIES)
        message(FATAL_ERROR "Install Exiv2 >= 0.28 and LibRaw >= 0.22, or enable VIDEX_FETCH_IMAGE_DEPENDENCIES")
    endif()
    # Isolate upstream build flags from the application's Qt targets.
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/image-dependencies
                     ${CMAKE_BINARY_DIR}/image-dependencies)
endif()
if(TARGET PkgConfig::SYSTEM_RAW)
    add_library(VidexRaw ALIAS PkgConfig::SYSTEM_RAW)
else()
    add_library(VidexRaw ALIAS videx_raw)
endif()
