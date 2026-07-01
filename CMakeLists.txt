cmake_minimum_required(VERSION 3.15)
project(ObstacleDetection VERSION 1.0 LANGUAGES CXX)
 
########################################
# C++ standard
########################################
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
########################################
# Default build type
########################################
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release)
########################################
# Threads (pthreads)
# Pulls in -pthread for std::thread and the affinity/priority pthread_* calls.
########################################
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
 
########################################
# Driver sources
# CONFIGURE_DEPENDS makes CMake re-glob at build time when files are added/removed.
########################################
file(GLOB_RECURSE Driver_src CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/drivers/*.cpp"
)
########################################
# Unit test sources
# CONFIGURE_DEPENDS makes CMake re-glob at build time when files are added/removed.
########################################
file(GLOB_RECURSE UnitTest_src CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/UnitTest/*.cpp"
)
add_library(UNITTEST_subsystem STATIC ${UnitTest_src})
target_include_directories(UNITTEST_subsystem PUBLIC 
include
)
########################################
# Executable + its sources
########################################
add_executable(${PROJECT_NAME}
  src/main.cpp
  ${Driver_src}
  ${UnitTest_src}
)
 
target_include_directories(${PROJECT_NAME} PRIVATE
  include
)
 
target_link_libraries(${PROJECT_NAME} PRIVATE
UNITTEST_subsystem
Threads::Threads
)
 

