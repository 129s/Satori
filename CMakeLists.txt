cmake_minimum_required(VERSION 3.16)
project(Satori LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SATORI_FONTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/Fonts")
if (NOT EXISTS "${SATORI_FONTS_DIR}")
    message(FATAL_ERROR "Missing assets/Fonts directory: ${SATORI_FONTS_DIR}")
endif()

if (WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_library(SatoriCoreLib STATIC
    src/audio/WaveWriter.cpp
    src/dsp/Filter.cpp
    src/synthesis/KarplusStrongString.cpp
    src/synthesis/KarplusStrongSynth.cpp
)

target_include_directories(SatoriCoreLib PUBLIC src)

if (WIN32)
    set(NUNITO_FONT_FILE "${SATORI_FONTS_DIR}/Nunito-Regular.ttf")
    if (NOT EXISTS "${NUNITO_FONT_FILE}")
        message(FATAL_ERROR "Nunito font asset missing: ${NUNITO_FONT_FILE}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${NUNITO_FONT_FILE}")

    set(SATORI_GENERATED_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/win/resources")
    file(MAKE_DIRECTORY "${SATORI_GENERATED_RESOURCES_DIR}")
    file(TO_NATIVE_PATH "${NUNITO_FONT_FILE}" NUNITO_FONT_NATIVE_PATH)
    string(REPLACE "\\" "\\\\" NUNITO_FONT_NATIVE_ESC "${NUNITO_FONT_NATIVE_PATH}")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/resources/NunitoFont.rc.in"
        "${SATORI_GENERATED_RESOURCES_DIR}/NunitoFont.rc"
        @ONLY)
    set(NUNITO_FONT_RC "${SATORI_GENERATED_RESOURCES_DIR}/NunitoFont.rc")

    add_library(SatoriRealtimeWin STATIC
        src/win/audio/RealtimeSynthRenderer.cpp
        src/win/audio/SatoriRealtimeEngine.cpp
        src/win/audio/WASAPIAudioEngine.cpp
    )
    target_include_directories(SatoriRealtimeWin PUBLIC src)
    target_link_libraries(SatoriRealtimeWin
        PUBLIC SatoriCoreLib
        PRIVATE ole32.lib mmdevapi.lib avrt.lib)

    add_executable(SatoriWinApp WIN32
        src/win/app/SatoriWinMain.cpp
        src/win/app/PresetManager.cpp
        src/win/ui/ParameterSlider.cpp
        src/win/ui/ParameterKnob.cpp
        src/win/ui/Direct2DContext.cpp
        src/win/ui/layout/UIStackPanel.cpp
        src/win/ui/layout/UIOverlay.cpp
        src/win/ui/nodes/ButtonBarNode.cpp
        src/win/ui/nodes/HeaderNode.cpp
        src/win/ui/nodes/SliderPanelNode.cpp
        src/win/ui/nodes/TextBlockNode.cpp
        src/win/ui/nodes/WaveformNode.cpp
        src/win/ui/nodes/TopBarNode.cpp
        src/win/ui/nodes/KnobPanelNode.cpp
        src/win/ui/nodes/FlowDiagramNode.cpp
        src/win/ui/nodes/KeyboardNode.cpp
        src/win/ui/VirtualKeyboard.cpp
        src/win/ui/WaveformView.cpp
        src/win/ui/UISkin.cpp
    )
    target_sources(SatoriWinApp PRIVATE "${NUNITO_FONT_RC}")
    target_link_libraries(SatoriWinApp PRIVATE SatoriRealtimeWin d2d1.lib dwrite.lib)

    add_executable(SatoriKnobSandbox WIN32
        src/win/app/KnobTestMain.cpp
        src/win/ui/ParameterKnob.cpp
    )
    target_sources(SatoriKnobSandbox PRIVATE "${NUNITO_FONT_RC}")
    target_include_directories(SatoriKnobSandbox PRIVATE src)
    target_link_libraries(SatoriKnobSandbox PRIVATE d2d1.lib dwrite.lib)
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

add_executable(SatoriUnitTests ${TEST_SOURCES})
target_link_libraries(SatoriUnitTests PRIVATE Catch2Amalgamated SatoriCoreLib)
if (WIN32)
    target_link_libraries(SatoriUnitTests PRIVATE SatoriRealtimeWin)
endif()

enable_testing()
add_test(NAME SatoriUnitTests COMMAND SatoriUnitTests)

add_executable(SatoriCLI
    src/main.cpp
)

target_link_libraries(SatoriCLI PRIVATE SatoriCoreLib)
