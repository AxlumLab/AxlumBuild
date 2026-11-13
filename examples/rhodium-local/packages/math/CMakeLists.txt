# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

cmake_minimum_required(VERSION 3.21)
project(SampleMath LANGUAGES CXX)
add_library(sample_math STATIC src/Add.cpp)
target_include_directories(sample_math PUBLIC include)
install(TARGETS sample_math ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
