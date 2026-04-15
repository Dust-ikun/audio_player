cmake_minimum_required(VERSION 3.10)
project(AudioPlayer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
    libswresample
)

find_package(SDL2 REQUIRED)

include_directories(${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})

add_executable(audio_player
    src/main.cpp
    src/audio_decoder.cpp
    src/demuxer.cpp
    src/video_decoder.cpp
)

target_link_libraries(audio_player
    ${FFMPEG_LIBRARIES}
    ${SDL2_LIBRARIES}
)
