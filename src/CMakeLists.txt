cmake_minimum_required(VERSION 2.8)
project( VidStream)

find_package( OpenCV REQUIRED )
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-app-1.0)

include_directories(${OpenCV_INCLUDE_DIRS} ${GST_INCLUDE_DIRS})
link_directories(${GST_LIBRARY_DIRS})

add_executable( stream main.cpp )
target_link_libraries( stream ${OpenCV_LIBS} ${GST_LIBRARIES})
