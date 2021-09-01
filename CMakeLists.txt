cmake_minimum_required(VERSION 3.21)
set(CMAKE_C_COMPILER clang CACHE STRING "" FORCE)
set(CMAKE_CXX_COMPILER clang++ CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD 20 CACHE STRING "" FORCE)

project(pdf_extractor)

find_package(fmt)
find_package(PkgConfig REQUIRED)
pkg_check_modules(poppler-cpp REQUIRED IMPORTED_TARGET poppler-cpp)

add_executable(main main.cpp)
target_link_libraries(
  main
  PRIVATE fmt::fmt
  PRIVATE PkgConfig::poppler-cpp
)
