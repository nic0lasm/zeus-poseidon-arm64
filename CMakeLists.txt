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
find_package(PkgConfig REQUIRED)
pkg_check_modules(UNICORN REQUIRED IMPORTED_TARGET unicorn)
# FFmpeg : celui du système par défaut ; -DZEUS_FFMPEG=DOSSIER désigne la version réduite et
# statique produite par tools/build_ffmpeg.py, seule redistribuable avec le reste. Elle est
# décrite à la main : pkg-config ne supporte pas les espaces dans les chemins.
set(ZEUS_FFMPEG "" CACHE PATH "Dossier d'un FFmpeg statique (tools/build_ffmpeg.py)")
if(ZEUS_FFMPEG)
    get_filename_component(ZEUS_FFMPEG_DIR "${ZEUS_FFMPEG}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    add_library(zeus_ffmpeg INTERFACE)
    target_include_directories(zeus_ffmpeg SYSTEM INTERFACE "${ZEUS_FFMPEG_DIR}/include")
    foreach(lib avformat avcodec swscale swresample avutil)
        target_link_libraries(zeus_ffmpeg INTERFACE "${ZEUS_FFMPEG_DIR}/lib/lib${lib}.a")
    endforeach()
    target_link_libraries(zeus_ffmpeg INTERFACE m "-framework CoreFoundation" "-framework CoreVideo"
        "-framework CoreMedia")
    add_library(PkgConfig::FFMPEG ALIAS zeus_ffmpeg)
else()
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavutil)
endif()

# Lecteurs des formats de fichiers du jeu, sans dépendance à la plateforme.
add_library(zeus_formats STATIC
    src/formats/mapfile.cpp
    src/formats/pkware.cpp
    src/formats/sg3.cpp)
target_include_directories(zeus_formats PUBLIC src)

# Logique du jeu reconstruite depuis l'analyse, sans dépendance à la plateforme.
add_library(zeus_game STATIC
    src/game/image_store.cpp
    src/game/terrain_view.cpp)
target_link_libraries(zeus_game PUBLIC zeus_formats)

# Tout ce qui remplace Win32/DirectX.
add_library(zeus_platform STATIC src/platform/capture.cpp)
target_include_directories(zeus_platform PUBLIC src)
target_link_libraries(zeus_platform PUBLIC SDL2::SDL2)

add_executable(zeus-sprites src/tools/sprite_viewer.cpp)
target_link_libraries(zeus-sprites PRIVATE zeus_formats zeus_platform ZLIB::ZLIB)

add_executable(zeus-map src/tools/map_viewer.cpp)
target_link_libraries(zeus-map PRIVATE zeus_game zeus_platform ZLIB::ZLIB)

# Hôte du code d'origine : processeur x86 émulé (Unicorn, GPL-2.0) et chargeur d'exécutable.
add_library(zeus_win32 STATIC
    src/win32/audio.cpp
    src/win32/bink.cpp
    src/win32/cpu.cpp
    src/win32/ddraw.cpp
    src/win32/desktop.cpp
    src/win32/kernel32.cpp
    src/win32/kernel32_files.cpp
    src/win32/kernel32_memory.cpp
    src/win32/language.cpp
    src/win32/media.cpp
    src/win32/mss.cpp
    src/win32/native.cpp
    src/win32/pe_image.cpp
    src/win32/process.cpp
    src/win32/text.cpp
    src/win32/user32.cpp
    src/win32/vfs.cpp
    src/win32/widescreen.cpp)
target_include_directories(zeus_win32 PUBLIC src)
target_link_libraries(zeus_win32 PUBLIC PkgConfig::UNICORN PkgConfig::FFMPEG SDL2::SDL2
    "-framework AudioToolbox" "-framework ImageIO" "-framework CoreGraphics" "-framework CoreFoundation")

add_executable(zeus-run src/tools/zeus_run.cpp)
target_link_libraries(zeus-run PRIVATE zeus_win32)

# Tests des pièces de l'hôte indépendantes du jeu (aucun fichier du jeu n'est lu).
add_executable(zeus-tests src/tests/zeus_tests.cpp)
target_link_libraries(zeus-tests PRIVATE zeus_win32)
target_compile_definitions(zeus-tests PRIVATE ZEUS_TEST_DIR="${CMAKE_BINARY_DIR}/tests-tmp")
enable_testing()
add_test(NAME zeus-tests COMMAND zeus-tests)
