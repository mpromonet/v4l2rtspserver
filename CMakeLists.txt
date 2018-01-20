cmake_minimum_required(VERSION 2.6)

# set project name from current directory
get_filename_component(BASENAME ${CMAKE_SOURCE_DIR} NAME)
project(${BASENAME})

option(COVERAGE "Coverage" OFF)

set(CMAKE_BUILD_TYPE DEBUG)
set(CMAKE_C_FLAGS "-Wall")
set(CMAKE_CXX_FLAGS "-std=c++11 -Wall")

find_package(Git)
if(GIT_FOUND)
    EXECUTE_PROCESS(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty OUTPUT_VARIABLE VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    STRING(REGEX REPLACE "^v(.*)" "\\1" VERSION "${VERSION}")
    add_definitions("-DVERSION=\"${VERSION}\"")
endif()


# define executable to build
include_directories("inc")
aux_source_directory(src SRC_FILES)
add_executable(${PROJECT_NAME} ${SRC_FILES})

if (COVERAGE)
	set(COVERAGE_FLAGS "-g -O0 -fprofile-arcs -ftest-coverage")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COVERAGE_FLAGS}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_FLAGS}")
	target_link_libraries(${PROJECT_NAME} gcov)
endif()

# v4l2wrapper
include_directories("v4l2wrapper/inc")
find_library(V4L2WRAPPER_LIBRARY libv4l2wrapper.a PATHS "." NO_CMAKE_FIND_ROOT_PATH)
if (NOT V4L2WRAPPER_LIBRARY)
        message(STATUS "Installing v4l2wrapper")
        EXEC_PROGRAM("git submodule init && git submodule update && make -C v4l2wrapper CC=${CMAKE_C_COMPILER} PREFIX=${CMAKE_FIND_ROOT_PATH} && cp v4l2wrapper/libv4l2wrapper.a . && make -C v4l2wrapper clean")
	find_library(V4L2WRAPPER_LIBRARY libv4l2wrapper.a PATHS "." NO_CMAKE_FIND_ROOT_PATH)
        message(STATUS "v4l2wrapper built in V4L2WRAPPER_LIBRARY=${V4L2WRAPPER_LIBRARY}")	
endif (NOT V4L2WRAPPER_LIBRARY)
target_link_libraries(${PROJECT_NAME} ${V4L2WRAPPER_LIBRARY})

#pthread
find_package (Threads)
target_link_libraries (${PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT}) 

#ALSA
find_package(ALSA QUIET)
if (ALSA_LIBRARY)
	message(STATUS "Alsa available")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHAVE_ALSA")
        target_link_libraries (${PROJECT_NAME} ${ALSA_LIBRARY}) 
	
	SET(CPACK_DEBIAN_PACKAGE_DEPENDS ${CPACK_DEBIAN_PACKAGE_DEPENDS}libasound2-dev,)	
endif (ALSA_LIBRARY)

# LOG4CPP
find_path(LOG4CPP_INCLUDE_DIR log4cpp/Category.hh)
if (LOG4CPP_INCLUDE_DIR)
        message(STATUS "Log4Cpp available")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHAVE_LOG4CPP")
	include_directories(${LOG4CPP_INCLUDE_DIR}) 

	find_library(LOG4CPP_LIBRARY log4cpp)
	target_link_libraries(${PROJECT_NAME} "${LOG4CPP_LIBRARY}")
	
	SET(CPACK_DEBIAN_PACKAGE_DEPENDS ${CPACK_DEBIAN_PACKAGE_DEPENDS}liblog4cpp5-dev,)
endif (LOG4CPP_INCLUDE_DIR)


# live555
find_path(LIBLIVE555_INCLUDE_DIR liveMedia/liveMedia.hh)
if (NOT LIBLIVE555_INCLUDE_DIR)
        message(STATUS "Installing live555")
        EXEC_PROGRAM("wget http://www.live555.com/liveMedia/public/live555-latest.tar.gz -O - | tar xzf -")
        EXEC_PROGRAM("cd live && ./genMakefiles linux-gdb && make CPLUSPLUS_COMPILER=${CMAKE_CXX_COMPILER} C_COMPILER=${CMAKE_C_COMPILER} LINK='${CMAKE_CXX_COMPILER} -o' CPPFLAGS=-DALLOW_RTSP_SERVER_PORT_REUSE=1 PREFIX=${CMAKE_FIND_ROOT_PATH} install")
else (NOT LIBLIVE555_INCLUDE_DIR)
	SET(CPACK_DEBIAN_PACKAGE_DEPENDS ${CPACK_DEBIAN_PACKAGE_DEPENDS}liblivemedia-dev,)
endif (NOT LIBLIVE555_INCLUDE_DIR)

find_path(LIBLIVE555_INCLUDE_DIR liveMedia/liveMedia.hh)
foreach (LIBLIVE555_module groupsock liveMedia UsageEnvironment BasicUsageEnvironment)
	include_directories(${LIBLIVE555_INCLUDE_DIR}/${LIBLIVE555_module}) 
endforeach (LIBLIVE555_module)

set(LIBLIVE555_LIBRARIES "")
foreach (LIBLIVE555_module groupsock liveMedia UsageEnvironment BasicUsageEnvironment)
	find_library( ${LIBLIVE555_module}_LIBRARY ${LIBLIVE555_module} )
	if ( ${LIBLIVE555_module}_LIBRARY )
		set(LIBLIVE555_LIBRARIES ${LIBLIVE555_LIBRARIES} ${${LIBLIVE555_module}_LIBRARY})
	endif ( ${LIBLIVE555_module}_LIBRARY )
endforeach (LIBLIVE555_module)
target_link_libraries(${PROJECT_NAME} ${LIBLIVE555_LIBRARIES})

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
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/v4l2rtspserver.service DESTINATION ${SYSTEMD_SERVICES_INSTALL_DIR} COMPONENT data)
endif (SYSTEMD_FOUND)

# package
install (TARGETS ${PROJECT_NAME} DESTINATION bin)

SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Michel Promonet") 
SET(CPACK_PACKAGE_CONTACT "michel.promonet@free.fr") 
SET(CPACK_SYSTEM_NAME ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR})
STRING(REGEX REPLACE ",$" "" CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}")
find_package(Git)
if(VERSION)
    SET(CPACK_PACKAGE_VERSION "${VERSION}")
endif()
INCLUDE(CPack)

