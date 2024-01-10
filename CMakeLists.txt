cmake_minimum_required(VERSION 3.17)

project(
	tiny_wm
	LANGUAGES CXX C
	DESCRIPTION "A tiny WM, based on XCB."
	VERSION 0.1.0
)

option(CMAKE_EXPORT_COMPILE_CONMANDS ON)

set(main_name main)

file(GLOB_RECURSE main_headers inc/*.h inc/*.hpp)
aux_source_directory(src main_src)
add_executable(${main_name} main_src)

target_include_directories(${main_name} PRIVATE_HEADER inc)

target_compile_options(${main_name} PUBLIC -W -Wall -g)
target_compile_features(${main_name} PUBLIC cxx_std_11)
target_compile_features(${main_name} PUBLIC c_std_11)

find_package(glog CONFIG REQUIRED)
target_link_libraries(tinywm PRIVATE glog::glog)