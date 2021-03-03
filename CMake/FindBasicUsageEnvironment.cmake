include(GNUInstallDirs)

find_library(
    LIBBASICUSAGEENVIRONMENT_LIBRARY
    NAMES BasicUsageEnvironment
    HINTS ${PROJECT_BINARY_DIR}/live/)

find_path(LIBBASICUSAGEENVIRONMENT_INCLUDE_DIR
  NAMES BasicUsageEnvironment_version.hh
  HINTS ${PROJECT_BINARY_DIR}/live/ ${CMAKE_INSTALL_INCLUDEDIR}
  PATH_SUFFIXES BasicUsageEnvironment)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(libBasicUsageEnvironment DEFAULT_MSG
                                  LIBBASICUSAGEENVIRONMENT_LIBRARY
                                  LIBBASICUSAGEENVIRONMENT_INCLUDE_DIR)

mark_as_advanced(LIBBASICUSAGEENVIRONMENT_LIBRARY LIBBASICUSAGEENVIRONMENT_INCLUDE_DIR)

if(LIBBASICUSAGEENVIRONMENT_FOUND AND NOT TARGET libBasicUsageEnvironment::libBasicUsageEnvironment)
  add_library(libBasicUsageEnvironment::libBasicUsageEnvironment SHARED IMPORTED)
  set_target_properties(
    libBasicUsageEnvironment::libBasicUsageEnvironment
    PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LIBBASICUSAGEENVIRONMENT_INCLUDE_DIR}"
      IMPORTED_LOCATION ${LIBBASICUSAGEENVIRONMENT_LIBRARY})
endif()
