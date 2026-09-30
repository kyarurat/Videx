# Supply MPV_ROOT for a Windows development SDK (include/mpv and lib).
# Ubuntu uses the system libmpv-dev package.
set(MPV_ROOT "" CACHE PATH "libmpv development SDK directory")
find_path(MPV_INCLUDE_DIR mpv/client.h HINTS "${MPV_ROOT}/include")
find_library(MPV_LIBRARY NAMES mpv libmpv mpv-2 libmpv-2
    HINTS "${MPV_ROOT}/lib" "${MPV_ROOT}")
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Mpv REQUIRED_VARS MPV_INCLUDE_DIR MPV_LIBRARY)
if(NOT Mpv_FOUND)
    message(FATAL_ERROR "libmpv is required. Install libmpv-dev on Ubuntu or set MPV_ROOT to a Windows SDK.")
endif()
add_library(VidexMpv UNKNOWN IMPORTED)
set_target_properties(VidexMpv PROPERTIES IMPORTED_LOCATION "${MPV_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MPV_INCLUDE_DIR}")
if(WIN32)
    find_file(MPV_RUNTIME NAMES libmpv-2.dll mpv-2.dll libmpv.dll
        HINTS "${MPV_ROOT}" "${MPV_ROOT}/bin")
    if(NOT MPV_RUNTIME)
        message(FATAL_ERROR "The Windows libmpv runtime DLL is missing from MPV_ROOT.")
    endif()
endif()
