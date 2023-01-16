cmake_minimum_required(VERSION 3.0.2)
project(stereo_dso_ros)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_GENERATOR "Ninja")

#set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake_module) # added by huicanlin
# set(OpenCV_DIR "/home/huicanlin/Downloads/software/opencv-3.2.0/local/share/OpenCV/")
find_package(OpenCV 4 REQUIRED)
# message("\n\n---- FOUND OpenCV Headers at\n\"${OpenCV_INCLUDE_DIRS}\"")
# message("---- FOUND OpenCV Libs at\n\"${OpenCV_LIBS}\"")

find_package(catkin REQUIRED COMPONENTS
        geometry_msgs
        roscpp
        sensor_msgs
        tf2
        tf2_ros
        pcl_conversions
        cv_bridge
        message_filters
        )

set(STEREO_DSO_PATH "/home/ubuntu/dev/stereo_dso")
message("\n\n---- LOOKING FOR STEREO_DSO at\n\"${STEREO_DSO_PATH}\"")


find_package(Pangolin 0.2 REQUIRED)
message("\n\n---- FOUND Pangolin Headers at\n\"${Pangolin_INCLUDE_DIRS}\"")
message("---- FOUND Pangolin Libs at\n\"${Pangolin_LIBRARIES}\"")

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake_module)
find_package(Eigen3 REQUIRED)
find_package(PCL 1.8 REQUIRED)
find_package(Boost COMPONENTS system thread filesystem chrono serialization date_time timer regex)
find_library(STEREO_DSO_LIBRARY dso ${STEREO_DSO_PATH}/build/lib)

catkin_package(
        CATKIN_DEPENDS
        geometry_msgs
        tf2
        tf2_ros
        pcl_conversions
        roscpp
        sensor_msgs
        cv_bridge
        message_filters
)

###########
## Build ##
###########

add_definitions("-DENABLE_SSE")
set(CMAKE_CXX_FLAGS
        "${SSE_FLAGS}"
        )

#set(CMAKE_CXX_FLAGS
#        "${SSE_FLAGS}"
#        )

#set(CMAKE_CXX_FLAGS
#        "${SSE_FLAGS} -march=native"
#        )

#set(CMAKE_CXX_FLAGS
#        "${SSE_FLAGS} -O2 -g -std=c++0x -march=native -fno-omit-frame-pointer"
#        )

include_directories(
        include
        ${PROJECT_SOURCE_DIR}/src
        ${STEREO_DSO_PATH}/src
        ${STEREO_DSO_PATH}/thirdparty/Sophus
        ${Pangolin_INCLUDE_DIRS}
        ${EIGEN3_INCLUDE_DIR}
        ${PCL_INCLUDE_DIRS}
        ${catkin_INCLUDE_DIRS}
)

## Declare a C++ executable
add_executable(stereo_dso_ros src/stereo_dso_ros.cpp src/ROSOutputWrapper.cpp)

set(STEREO_DSO_LINKED_LIBRARIES boost_system cxsparse ${BOOST_THREAD_LIBRARY} ${LIBZIP_LIBRARY} ${Pangolin_LIBRARIES} ${PCL_LIBRARIES} ${OpenCV_LIBS})

target_link_libraries(stereo_dso_ros
        ${STEREO_DSO_LIBRARY}
        ${STEREO_DSO_LINKED_LIBRARIES}
        ${catkin_LIBRARIES})
