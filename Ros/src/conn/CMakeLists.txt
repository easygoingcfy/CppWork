cmake_minimum_required(VERSION 3.5.1)

project(cfy_conn)

add_executable(conn_client src/test.cc src/tcp.cpp src/interface.cpp src/udp.cpp)

add_library(cfy_conn_lib src/tcp.cpp src/interface.cpp src/udp.cpp)

find_package(catkin REQUIRED COMPONENTS genmsg actionlib_msgs actionlib roscpp)
find_package(Boost 1.55.0 REQUIRED COMPONENTS system filesystem thread)

include_directories(include ${Boost_INCLUDE_DIRS} ${catkin_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})
target_link_libraries(conn_client ${Boost_LIBRARIES} ${catkin_LIBRARIES})

catkin_package(INCLUDE_DIRS include
LIBRARIES cfy_conn_lib)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
