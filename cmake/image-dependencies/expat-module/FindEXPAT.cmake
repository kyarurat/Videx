# Exiv2's embedded XMP SDK needs headers as well as the library target.
set(EXPAT_FOUND TRUE)
set(EXPAT_VERSION_STRING 2.8.5)
set(EXPAT_INCLUDE_DIR "${videx_expat_SOURCE_DIR}/expat/lib")
set(EXPAT_INCLUDE_DIRS "${EXPAT_INCLUDE_DIR}")
set(EXPAT_LIBRARIES EXPAT::EXPAT)
