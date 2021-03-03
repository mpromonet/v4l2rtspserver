include(GNUInstallDirs)

find_library(
    LIBGROUPSOCK_LIBRARY
    NAMES groupsock
    HINTS ${PROJECT_BINARY_DIR}/live/)

find_path(LIBGROUPSOCK_INCLUDE_DIR
  NAMES groupsock_version.hh
  HINTS ${PROJECT_BINARY_DIR}/live/ ${CMAKE_INSTALL_INCLUDEDIR}
  PATH_SUFFIXES groupsock)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(libgroupsock DEFAULT_MSG
                                  LIBGROUPSOCK_LIBRARY
                                  LIBGROUPSOCK_INCLUDE_DIR)

mark_as_advanced(LIBGROUPSOCK_LIBRARY LIBGROUPSOCK_INCLUDE_DIR)

if(LIBGROUPSOCK_FOUND AND NOT TARGET libgroupsock::libgroupsock)
  add_library(libgroupsock::libgroupsock SHARED IMPORTED)
  set_target_properties(
    libgroupsock::libgroupsock
    PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LIBGROUPSOCK_INCLUDE_DIR}"
      IMPORTED_LOCATION ${LIBGROUPSOCK_LIBRARY})
endif()
