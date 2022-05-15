include(GNUInstallDirs)

find_library(
    LIBLIVEMEDIA_LIBRARY
    NAMES liveMedia
    HINTS ${PROJECT_BINARY_DIR}/live/)

find_path(LIBLIVEMEDIA_INCLUDE_DIR
  NAMES liveMedia_version.hh
  HINTS ${PROJECT_BINARY_DIR}/live/ ${CMAKE_INSTALL_INCLUDEDIR}
  PATH_SUFFIXES liveMedia)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(libliveMedia DEFAULT_MSG
                                  LIBLIVEMEDIA_LIBRARY
                                  LIBLIVEMEDIA_INCLUDE_DIR)

mark_as_advanced(LIBLIVEMEDIA_LIBRARY LIBLIVEMEDIA_INCLUDE_DIR)

if(LIBLIVEMEDIA_FOUND AND NOT TARGET libliveMedia::libliveMedia)
  add_library(libliveMedia::libliveMedia SHARED IMPORTED)
  set_target_properties(
    libliveMedia::libliveMedia
    PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LIBLIVEMEDIA_INCLUDE_DIR}"
      IMPORTED_LOCATION ${LIBLIVEMEDIA_LIBRARY})
endif()
