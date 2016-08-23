cmake_minimum_required (VERSION 2.6)

project (pusher)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_library(GLOG_LIBRARY_PATH glog)
find_library(GFLAGS_LIBRARY_PATH gflags)

add_compile_options(-std=c++14)
add_compile_options(-Wall)
#add_compile_options(-Wextra)
#add_compile_options(-Wformat-security)
#add_compile_options(-Wpedantic)
#add_compile_options(-Werror)
add_compile_options(-g)
add_compile_options(-O3)
add_compile_options(-fdiagnostics-color=auto)

add_definitions(-DGOOGLE_STRIP_LOG=0)

include_directories(~/opt/include)
link_directories(~/opt/lib)

add_subdirectory(src)
