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
        src/win/ui/ParameterSlider.cpp
        src/win/ui/Direct2DContext.cpp
    )
    target_link_libraries(SatoriWin PRIVATE SatoriWinAudio d2d1.lib dwrite.lib)
endif()

add_library(Catch2Amalgamated STATIC
    third_party/catch2/catch_amalgamated.cpp
)
target_include_directories(Catch2Amalgamated PUBLIC third_party/catch2)

set(TEST_SOURCES
    tests/main.cpp
    tests/core_tests.cpp
)
if (WIN32)
    list(APPEND TEST_SOURCES tests/win_audio_tests.cpp)
endif()

add_executable(SatoriTests ${TEST_SOURCES})
target_link_libraries(SatoriTests PRIVATE Catch2Amalgamated SatoriCore)
if (WIN32)
    target_link_libraries(SatoriTests PRIVATE SatoriWinAudio)
endif()

enable_testing()
add_test(NAME SatoriTests COMMAND SatoriTests)

add_executable(Satori
    src/main.cpp
)

target_link_libraries(Satori PRIVATE SatoriCore)
