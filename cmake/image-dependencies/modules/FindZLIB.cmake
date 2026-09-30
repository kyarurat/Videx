# Adapter for upstream projects when zlib is built in this same CMake graph.
set(ZLIB_FOUND TRUE)
set(ZLIB_VERSION_STRING 1.3.1)
set(ZLIB_VERSION 1.3.1)
set(ZLIB_INCLUDE_DIRS "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}")
set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}")
set(ZLIB_LIBRARIES ZLIB::ZLIB)
