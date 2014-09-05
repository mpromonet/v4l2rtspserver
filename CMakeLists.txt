cmake_minimum_required(VERSION 2.6)

# set project name from current directory
get_filename_component(BASENAME ${CMAKE_SOURCE_DIR} NAME)
project(${BASENAME})

set(CMAKE_BUILD_TYPE DEBUG)
set(CMAKE_C_FLAGS "-Wall")
set(CMAKE_CXX_FLAGS "-Wall")

# define executable to build
include_directories("${PROJECT_BINARY_DIR}/inc")
aux_source_directory(src SRC_FILES)
add_executable(${PROJECT_NAME} ${SRC_FILES})

# V4L2
find_path(V4L2_INCLUDE_DIR libv4l2.h)
if (NOT V4L2_INCLUDE_DIR)
	message(STATUS "Could not find the libv4l-dev.")
	EXEC_PROGRAM("sudo apt-get install libv4l-dev")
	find_path(V4L2_INCLUDE_DIR libv4l2.h)
endif (NOT V4L2_INCLUDE_DIR)
include_directories(${V4L2_INCLUDE_DIR}) 

find_library(V4L2_LIBRARY v4l2)
target_link_libraries(${PROJECT_NAME} "${V4L2_LIBRARY}")

# live555
find_path(LIBLIVE555_INCLUDE_DIR liveMedia/liveMedia.hh)
if (NOT LIBLIVE555_INCLUDE_DIR)
        message(STATUS "Could not find the liblivemedia-dev")
        EXEC_PROGRAM("sudo apt-get install liblivemedia-dev")
        find_path(V4L2_INCLUDE_DIR liveMedia/liveMedia.hh)
endif (NOT LIBLIVE555_INCLUDE_DIR)
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

# package
install (TARGETS ${PROJECT_NAME} DESTINATION bin)

SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Michel Promonet") 
SET(CPACK_PACKAGE_CONTACT "michel.promonet@free.fr") 

INCLUDE(CPack)

