cmake_minimum_required(VERSION 3.16)
project(Satori LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(SatoriCore STATIC
    src/audio/WaveWriter.cpp
    src/dsp/Filter.cpp
    src/synthesis/KarplusStrongString.cpp
    src/synthesis/KarplusStrongSynth.cpp
)

target_include_directories(SatoriCore PUBLIC src)

if (WIN32)
    add_library(SatoriWinAudio STATIC
        src/win/audio/RealtimeSynthRenderer.cpp
        src/win/audio/SatoriRealtimeEngine.cpp
        src/win/audio/WASAPIAudioEngine.cpp
    )
    target_include_directories(SatoriWinAudio PUBLIC src)
    target_link_libraries(SatoriWinAudio
        PUBLIC SatoriCore
        PRIVATE ole32.lib mmdevapi.lib avrt.lib)

    add_executable(SatoriWin WIN32
        src/win/app/SatoriWinMain.cpp
    )
    target_link_libraries(SatoriWin PRIVATE SatoriWinAudio)
endif()

add_executable(Satori
    src/main.cpp
)

target_link_libraries(Satori PRIVATE SatoriCore)
