cmake_minimum_required(VERSION 3.20)
project(zeus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(SDL2 REQUIRED)
find_package(ZLIB REQUIRED)

# Lecteurs des formats de fichiers du jeu, sans dépendance à la plateforme.
add_library(zeus_formats STATIC src/formats/sg3.cpp)
target_include_directories(zeus_formats PUBLIC src)

add_executable(zeus-sprites src/tools/sprite_viewer.cpp)
target_link_libraries(zeus-sprites PRIVATE zeus_formats SDL2::SDL2 ZLIB::ZLIB)
