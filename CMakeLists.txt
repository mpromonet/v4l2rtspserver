cmake_minimum_required(VERSION 3.12)

project(v4l2rtspserver)

option(COVERAGE "Coverage" OFF)
option (WITH_SSL "Enable SSL support" ON)

set(ALSA ON CACHE BOOL "use ALSA if available")
set(STATICSTDCPP ON CACHE BOOL "use gcc static lib if available")
set(LOG4CPP OFF CACHE BOOL "use log4cpp if available")
set(LIVE555URL http://www.live555.com/liveMedia/public/live555-latest.tar.gz CACHE STRING "live555 url")
set(LIVE555CFLAGS -DBSD=1 -DSOCKLEN_T=socklen_t -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE=1 -DALLOW_RTSP_SERVER_PORT_REUSE=1 -DNO_STD_LIB=1 CACHE STRING "live555 CFGLAGS")
set(SYSTEMD ON CACHE BOOL "install SystemD service")

set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake/")

find_package(Git)
if(DEFINED ENV{V4L2RTSPSERVER_VERSION})
    # set version from V4L2RTSPSERVER_VERSION env variable
    add_compile_definitions("VERSION=\"$ENV{V4L2RTSPSERVER_VERSION}\"")
elseif(GIT_FOUND)
    # set version based on git
    EXECUTE_PROCESS(COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_CURRENT_SOURCE_DIR} describe --tags --always --dirty OUTPUT_VARIABLE VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    STRING(REGEX REPLACE "^v(.*)" "\\1" VERSION "${VERSION}")
    add_compile_definitions("VERSION=\"${VERSION}\"")
else()
    message(WARNING "VERSION is undefined")
    add_compile_definitions("VERSION=\"undefined\"")
endif()

# define executable to build
include_directories("inc")
add_executable(${PROJECT_NAME} main.cpp)

if (COVERAGE)
	set(COVERAGE_FLAGS "-g -O0 -fprofile-arcs -ftest-coverage")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COVERAGE_FLAGS}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_FLAGS}")
	target_link_libraries(${PROJECT_NAME} gcov)
endif()

message(STATUS "CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
message(STATUS "CMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")


# openssl ?
if (WITH_SSL)
  find_package(OpenSSL QUIET)
endif()
MESSAGE("OpenSSL_FOUND = ${OpenSSL_FOUND}")

# live555
find_package(PkgConfig)
pkg_check_modules(live555 QUIET live555)
MESSAGE("live555_FOUND = ${live555_FOUND}")
if (live555_FOUND)
    message(STATUS "live555 found")
    set(LIVEINCLUDE ${live555_INCLUDE_DIRS})
    SET(LIVE_LIBRARIES ${live555_LIBRARIES}) 
    SET(CPACK_DEBIAN_PACKAGE_DEPENDS ${CPACK_DEBIAN_PACKAGE_DEPENDS}livemedia-utils,)
else()
    set(LIVE ${CMAKE_BINARY_DIR}/live)
    set(LIVEINCLUDE ${LIVE}/groupsock/include ${LIVE}/liveMedia/include ${LIVE}/UsageEnvironment/include ${LIVE}/BasicUsageEnvironment/include)
    if (NOT EXISTS ${LIVE})
        file (DOWNLOAD ${LIVE555URL} ${CMAKE_BINARY_DIR}/live555-latest.tar.gz )
        message(STATUS "extract live555")
        EXECUTE_PROCESS(COMMAND ${CMAKE_COMMAND} -E tar xf ${CMAKE_BINARY_DIR}/live555-latest.tar.gz  RESULT_VARIABLE unpack_result)
        if(NOT unpack_result STREQUAL "0")
            message(FATAL_ERROR "Fetching and compiling live555 failed!")
        endif()
    endif()
    FILE(GLOB LIVESOURCE ${LIVE}/groupsock/*.c* ${LIVE}/liveMedia/*.c* ${LIVE}/UsageEnvironment/*.c* ${LIVE}/BasicUsageEnvironment/*.c*)
endif()
if (NOT OpenSSL_FOUND)
    set(LIVE555CFLAGS ${LIVE555CFLAGS} -DNO_OPENSSL=1)
endif()

# librtsp
aux_source_directory(src LIBV4L2RTSP_SRC_FILES)
add_library (libv4l2rtspserver STATIC ${LIVESOURCE} ${LIBV4L2RTSP_SRC_FILES})	
target_include_directories(libv4l2rtspserver PUBLIC inc ${LIVEINCLUDE})
target_compile_definitions(libv4l2rtspserver PUBLIC ${LIVE555CFLAGS})
target_link_libraries (${PROJECT_NAME} libv4l2rtspserver ${LIVE_LIBRARIES}) 
set (LIBRARIES "")
if (OpenSSL_FOUND)
    set(LIBRARIES ${LIBRARIES} OpenSSL::SSL)
endif ()

#pthread
find_package (Threads)
target_link_libraries (libv4l2rtspserver PUBLIC Threads::Threads) 

# LOG4CPP
if (LOG4CPP) 
    find_library(LOG4CPP_LIBRARY NAMES log4cpp)
    if (LOG4CPP_LIBRARY)
        message(STATUS "Log4Cpp available ${LOG4CPP_LIBRARY}")
        target_compile_definitions(libv4l2rtspserver PUBLIC HAVE_LOG4CPP)
        target_link_libraries(libv4l2rtspserver PUBLIC "${LOG4CPP_LIBRARY}")
        
        SET(CPACK_DEBIAN_PACKAGE_DEPENDS ${CPACK_DEBIAN_PACKAGE_DEPENDS}liblog4cpp5v5,)
    endif ()
endif ()

#ALSA
if (ALSA)
    find_package(ALSA QUIET)
    MESSAGE("ALSA_FOUND = ${ALSA_FOUND}")
    if (ALSA_LIBRARY)
        target_compile_definitions(libv4l2rtspserver PUBLIC HAVE_ALSA)
        set(LIBRARIES ${LIBRARIES} ALSA::ALSA)

        SET(CPACK_DEBIAN_PACKAGE_DEPENDS ${CPACK_DEBIAN_PACKAGE_DEPENDS}libasound2,)	
    endif ()
endif()

# libv4l2cpp
if (GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init)
endif()
add_subdirectory(libv4l2cpp EXCLUDE_FROM_ALL)
target_include_directories(libv4l2rtspserver PUBLIC libv4l2cpp/inc)
target_link_libraries (libv4l2rtspserver PUBLIC libv4l2cpp ${LIBRARIES}) 

# static link of stdc++ if available
if (STATICSTDCPP)
  include(CheckCXXCompilerFlag)
  CHECK_CXX_COMPILER_FLAG("-static-libgcc" CXX_SUPPORTS_STATIC_LIBGCC)
  if (CXX_SUPPORTS_STATIC_LIBGCC) 
    target_link_libraries (${PROJECT_NAME} -static-libgcc)
  endif()    
  CHECK_CXX_COMPILER_FLAG("-static-libstdc++" CXX_SUPPORTS_STATIC_LIBSTDCPP)
  if (CXX_SUPPORTS_STATIC_LIBSTDCPP) 
    target_link_libraries (${PROJECT_NAME} -static-libstdc++)
  endif()
endif()

#testing
enable_testing()
add_test(help ./${PROJECT_NAME} -h)

#systemd
if (SYSTEMD)
    find_package(PkgConfig)
    pkg_check_modules(SYSTEMD systemd QUIET)
    if (SYSTEMD_FOUND)
        message(STATUS "SystemD available")
        execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=systemdsystemunitdir systemd OUTPUT_VARIABLE SYSTEMD_SERVICES_INSTALL_DIR)
        string(REGEX REPLACE "[ \t\n]+" "" SYSTEMD_SERVICES_INSTALL_DIR "${SYSTEMD_SERVICES_INSTALL_DIR}")
        message(STATUS "SystemD directory '${SYSTEMD_SERVICES_INSTALL_DIR}'")
        
        configure_file(v4l2rtspserver.service.in ${CMAKE_CURRENT_BINARY_DIR}/v4l2rtspserver.service @ONLY)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/v4l2rtspserver.service DESTINATION ${SYSTEMD_SERVICES_INSTALL_DIR})
    endif (SYSTEMD_FOUND)
endif (SYSTEMD)

# package
install (TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install (FILES index.html DESTINATION share/${PROJECT_NAME}/)
install (FILES hls.js DESTINATION share/${PROJECT_NAME}/)

SET(CPACK_GENERATOR "DEB")
IF(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
    SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "armv[6-7].*")
    SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE armhf)
elseif(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
    SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
endif()
message(STATUS "CPACK_DEBIAN_PACKAGE_ARCHITECTURE=${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Michel Promonet") 
SET(CPACK_PACKAGE_CONTACT "michel.promonet@free.fr") 
SET(CPACK_SYSTEM_NAME ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_BUILD_TYPE})
STRING(REGEX REPLACE ",$" "" CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}")
SET(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
if(VERSION)
    SET(CPACK_PACKAGE_VERSION "${VERSION}")
endif()
INCLUDE(CPack)

