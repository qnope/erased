cmake_minimum_required(VERSION 3.5)

project(Erased LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Erased main.cpp erased/erased.h)


