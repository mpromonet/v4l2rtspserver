cmake_minimum_required(VERSION 3.0)

project(v4l2rtspserver)

option(COVERAGE "Coverage" OFF)

set(ALSA ON CACHE BOOL "use ALSA is available")
set(LIVE555URL http://www.live555.com/liveMedia/public/live555-latest.tar.gz CACHE STRING "live555 url")
set(LIVE555CFLAGS -DBSD=1 -DSOCKLEN_T=socklen_t -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE=1 -DALLOW_RTSP_SERVER_PORT_REUSE=1 -DNO_OPENSSL=1 CACHE STRING "live555 CFGLAGS")

set(CMAKE_CXX_STANDARD 11)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake/")

# set version based on git
find_package(Git)
if(GIT_FOUND)
    EXECUTE_PROCESS(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty OUTPUT_VARIABLE VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    STRING(REGEX REPLACE "^v(.*)" "\\1" VERSION "${VERSION}")
    add_definitions("-DVERSION=\"${VERSION}\"")
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

#pthread
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package (Threads)
target_link_libraries (${PROJECT_NAME} Threads::Threads) 


# LOG4CPP
find_library(LOG4CPP_LIBRARY NAMES log4cpp)
if (LOG4CPP_LIBRARY)
	message(STATUS "Log4Cpp available ${LOG4CPP_LIBRARY}")
	add_definitions(-DHAVE_LOG4CPP)
	target_link_libraries(${PROJECT_NAME} "${LOG4CPP_LIBRARY}")
	
	SET(CPACK_DEBIAN_PACKAGE_DEPENDS ${CPACK_DEBIAN_PACKAGE_DEPENDS}liblog4cpp5-dev,)
endif ()


# live555
find_package(LiveMedia)
find_package(UsageEnvironment)
find_package(BasicUsageEnvironment)
find_package(GroupSock)
if (LIBLIVEMEDIA_FOUND AND LIBBASICUSAGEENVIRONMENT_FOUND AND LIBUSAGEENVIRONMENT_FOUND AND LIBGROUPSOCK_FOUND)
    message(STATUS "live555 loaded")
    set(LIVEINCLUDE ${LIBLIVEMEDIA_INCLUDE_DIR} ${LIBBASICUSAGEENVIRONMENT_INCLUDE_DIR} ${LIBUSAGEENVIRONMENT_INCLUDE_DIR} ${LIBGROUPSOCK_INCLUDE_DIR})
    target_link_libraries (${PROJECT_NAME} liveMedia UsageEnvironment BasicUsageEnvironment groupsock) 
    SET(CPACK_DEBIAN_PACKAGE_DEPENDS ${CPACK_DEBIAN_PACKAGE_DEPENDS}livemedia-utils,)
else()
    set(LIVE ${CMAKE_BINARY_DIR}/live)
    set(LIVEINCLUDE ${LIVE}/groupsock/include ${LIVE}/liveMedia/include ${LIVE}/UsageEnvironment/include ${LIVE}/BasicUsageEnvironment/include)
    if (NOT EXISTS ${LIVE})
        file (DOWNLOAD ${LIVE555URL} ${CMAKE_BINARY_DIR}/live555-latest.tar.gz )
        EXECUTE_PROCESS(COMMAND ${CMAKE_COMMAND} -E tar xvf ${CMAKE_BINARY_DIR}/live555-latest.tar.gz  RESULT_VARIABLE unpack_result)
        if(NOT unpack_result STREQUAL "0")
            message(FATAL_ERROR "Fetching and compiling live555 failed!")
        endif()
    endif()
    FILE(GLOB LIVESOURCE ${LIVE}/groupsock/*.c* ${LIVE}/liveMedia/*.c* ${LIVE}/UsageEnvironment/*.c* ${LIVE}/BasicUsageEnvironment/*.c*)
    add_definitions(${LIVE555CFLAGS})
endif()

# librtsp
aux_source_directory(src SRC_FILES)
add_library (libv4l2rtspserver STATIC ${LIVESOURCE} ${SRC_FILES})	
target_include_directories(libv4l2rtspserver PUBLIC inc ${LIVEINCLUDE})
target_link_libraries (${PROJECT_NAME} libv4l2rtspserver) 

#ALSA
if (ALSA)
find_package(ALSA QUIET)
if (ALSA_LIBRARY)
	message(STATUS "Alsa available")
	add_definitions(-DHAVE_ALSA)
    target_link_libraries (${PROJECT_NAME} ${ALSA_LIBRARY}) 
	
	SET(CPACK_DEBIAN_PACKAGE_DEPENDS ${CPACK_DEBIAN_PACKAGE_DEPENDS}libasound2,)	
endif (ALSA_LIBRARY)
endif()

# v4l2wrapper
EXEC_PROGRAM("git submodule update --init")
aux_source_directory(v4l2wrapper/src LIBSRC_FILES)
include_directories("v4l2wrapper/inc")
add_library(v4l2wrapper STATIC ${LIBSRC_FILES})
target_link_libraries (${PROJECT_NAME} v4l2wrapper) 

#testing
enable_testing()
add_test(help ./${PROJECT_NAME} -h)

#systemd
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

# package
install (TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install (FILES index.html DESTINATION share/${PROJECT_NAME}/)
install (FILES hls.js/dist/hls.light.min.js DESTINATION share/${PROJECT_NAME}/hls.js/dist/)

SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Michel Promonet") 
SET(CPACK_PACKAGE_CONTACT "michel.promonet@free.fr") 
SET(CPACK_SYSTEM_NAME ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR})
STRING(REGEX REPLACE ",$" "" CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}")
SET(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
if(VERSION)
    SET(CPACK_PACKAGE_VERSION "${VERSION}")
endif()
INCLUDE(CPack)

