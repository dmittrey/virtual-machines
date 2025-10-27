cmake_minimum_required(VERSION 3.11)
project("l1-cache-info")

add_executable(${PROJECT_NAME} "l1-cache-info.cc")

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)