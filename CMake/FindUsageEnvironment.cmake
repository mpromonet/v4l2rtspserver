include(GNUInstallDirs)

find_library(
    LIBUSAGEENVIRONMENT_LIBRARY
    NAMES UsageEnvironment
    HINTS ${PROJECT_BINARY_DIR}/live/)

find_path(LIBUSAGEENVIRONMENT_INCLUDE_DIR
  NAMES UsageEnvironment_version.hh
  HINTS ${PROJECT_BINARY_DIR}/live/ ${CMAKE_INSTALL_INCLUDEDIR}
  PATH_SUFFIXES UsageEnvironment)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(libUsageEnvironment DEFAULT_MSG
                                  LIBUSAGEENVIRONMENT_LIBRARY
                                  LIBUSAGEENVIRONMENT_INCLUDE_DIR)

mark_as_advanced(LIBUSAGEENVIRONMENT_LIBRARY LIBUSAGEENVIRONMENT_INCLUDE_DIR)

if(LIBUSAGEENVIRONMENT_FOUND AND NOT TARGET libUsageEnvironment::libUsageEnvironment)
  add_library(libUsageEnvironment::libUsageEnvironment SHARED IMPORTED)
  set_target_properties(
    libUsageEnvironment::libUsageEnvironment
    PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LIBUSAGEENVIRONMENT_INCLUDE_DIR}"
      IMPORTED_LOCATION ${LIBUSAGEENVIRONMENT_LIBRARY})
endif()
