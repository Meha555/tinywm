cmake_minimum_required(VERSION 3.13)

project(
	tiny_wm
	LANGUAGES CXX C
	DESCRIPTION "A tiny WM, based on XCB."
	VERSION 0.1.0
)

option(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(main_name tinywm)
set(CMAKE_BUILD_TYPE Debug)

file(GLOB_RECURSE main_headers inc/*.h inc/*.hpp)
aux_source_directory(src main_src)

add_executable(${main_name} ${main_src} main.cpp)

target_include_directories(${main_name} PRIVATE inc)

target_compile_options(${main_name} PUBLIC -W -w -Wall)
target_compile_features(${main_name} PUBLIC cxx_std_11)
target_compile_features(${main_name} PUBLIC c_std_11)

# find_package(glog REQUIRED)
target_link_libraries(${main_name} PRIVATE glog)
target_link_libraries(${main_name} PRIVATE xcb xcb-keysyms xcb-util xcb-icccm X11)