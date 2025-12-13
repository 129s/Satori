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

if (MSVC)
    # Treat all sources as UTF-8 to avoid codepage-related parse issues on Windows.
    add_compile_options(/utf-8)
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_library(SatoriCoreLib STATIC
    src/audio/WaveWriter.cpp
    src/dsp/Filter.cpp
    src/dsp/Fft.cpp
    src/dsp/PartitionedConvolver.cpp
    src/dsp/ConvolutionReverb.cpp
    src/dsp/RoomIrLibrary.cpp
    src/engine/StringParams.cpp
    src/engine/StringSynthEngine.cpp
    src/synthesis/KarplusStrongString.cpp
    src/synthesis/KarplusStrongSynth.cpp
)

target_include_directories(SatoriCoreLib PUBLIC src)

# Build-time embedding of IR WAVs into compiled C++ arrays (no runtime file IO).
set(SATORI_IR_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/ir_src")
file(GLOB SATORI_IR_INPUTS CONFIGURE_DEPENDS
    "${SATORI_IR_SRC_DIR}/*.wav"
    "${SATORI_IR_SRC_DIR}/*.wv"
)
if (NOT SATORI_IR_INPUTS)
    message(FATAL_ERROR "Missing room IR audio files: ${SATORI_IR_SRC_DIR}/*.wav or *.wv")
endif()
set(SATORI_IR_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/room_ir")
set(SATORI_IR_DATA_H "${SATORI_IR_GEN_DIR}/RoomIrData.h")
set(SATORI_IR_DATA_CPP "${SATORI_IR_GEN_DIR}/RoomIrData.cpp")
add_custom_command(
    OUTPUT "${SATORI_IR_DATA_H}" "${SATORI_IR_DATA_CPP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SATORI_IR_GEN_DIR}"
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_room_ir_data.py"
            --input_dir "${SATORI_IR_SRC_DIR}"
            --out_h "${SATORI_IR_DATA_H}"
            --out_cpp "${SATORI_IR_DATA_CPP}"
            --preview 512
    DEPENDS ${SATORI_IR_INPUTS}
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_room_ir_data.py"
    VERBATIM
)
add_custom_target(SatoriRoomIrData DEPENDS "${SATORI_IR_DATA_H}" "${SATORI_IR_DATA_CPP}")
add_dependencies(SatoriCoreLib SatoriRoomIrData)
target_sources(SatoriCoreLib PRIVATE "${SATORI_IR_DATA_CPP}" "${SATORI_IR_DATA_H}")
target_include_directories(SatoriCoreLib PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")

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
        src/win/audio/SatoriRealtimeEngine.cpp
        src/win/audio/UnifiedAudioEngine.cpp
        src/win/audio/WASAPIAudioEngine.cpp
        src/win/audio/AsioAudioEngine.cpp
    )
    target_include_directories(SatoriRealtimeWin PUBLIC src)
    target_link_libraries(SatoriRealtimeWin
        PUBLIC SatoriCoreLib
        PRIVATE ole32.lib mmdevapi.lib avrt.lib advapi32.lib)

    option(SATORI_DISABLE_ASIO "Disable ASIO backend even if SDK is present" OFF)
    # Keep ASIO enabled by default (single-build setup). This is forced so that
    # existing build directories don't get stuck with an old cached OFF value.
    set(SATORI_ENABLE_ASIO ON CACHE BOOL
        "Enable ASIO backend (requires ASIO SDK integration)" FORCE)
    set(_SATORI_DEFAULT_ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ASIOSDK")
    set(SATORI_ASIO_SDK_DIR "${_SATORI_DEFAULT_ASIO_SDK_DIR}" CACHE PATH
        "Path to Steinberg ASIO SDK root (must contain common/asio.h)")
    # If an existing build directory cached an empty SDK path, recover to the
    # in-repo default so ASIO stays actually usable in a single `build/` setup.
    if (SATORI_ENABLE_ASIO AND (NOT SATORI_ASIO_SDK_DIR))
        set(SATORI_ASIO_SDK_DIR "${_SATORI_DEFAULT_ASIO_SDK_DIR}" CACHE PATH
            "Path to Steinberg ASIO SDK root (must contain common/asio.h)" FORCE)
    endif()
    if (SATORI_DISABLE_ASIO)
        set(SATORI_ENABLE_ASIO OFF CACHE BOOL
            "Enable ASIO backend (requires ASIO SDK integration)" FORCE)
    endif()
    if (SATORI_ENABLE_ASIO)
        target_compile_definitions(SatoriRealtimeWin PRIVATE SATORI_ENABLE_ASIO=1)
    else()
        target_compile_definitions(SatoriRealtimeWin PRIVATE SATORI_ENABLE_ASIO=0)
    endif()

    set(SATORI_HAS_ASIO_SDK 0)
    if (SATORI_ENABLE_ASIO AND SATORI_ASIO_SDK_DIR)
        if (EXISTS "${SATORI_ASIO_SDK_DIR}/common/asio.h" AND EXISTS "${SATORI_ASIO_SDK_DIR}/common/iasiodrv.h")
            set(SATORI_HAS_ASIO_SDK 1)
            target_include_directories(SatoriRealtimeWin PRIVATE "${SATORI_ASIO_SDK_DIR}/common")
        else()
            message(FATAL_ERROR
                "SATORI_ENABLE_ASIO=ON but ASIO SDK headers not found under: ${SATORI_ASIO_SDK_DIR}/common "
                "(expected asio.h and iasiodrv.h)")
        endif()
    endif()
    target_compile_definitions(SatoriRealtimeWin PRIVATE SATORI_HAS_ASIO_SDK=${SATORI_HAS_ASIO_SDK})

    add_executable(SatoriWinApp WIN32
        src/win/app/SatoriWinMain.cpp
        src/win/app/PresetManager.cpp
        src/win/ui/ParameterSlider.cpp
        src/win/ui/ParameterKnob.cpp
        src/win/ui/DebugOverlay.cpp
        src/win/ui/Direct2DContext.cpp
        src/win/ui/KeyboardKeymap.cpp
        src/win/ui/layout/UIStackPanel.cpp
        src/win/ui/layout/UIHorizontalStack.cpp
        src/win/ui/layout/UIOverlay.cpp
        src/win/ui/nodes/ButtonBarNode.cpp
        src/win/ui/nodes/HeaderNode.cpp
        src/win/ui/nodes/HeaderBarNode.cpp
        src/win/ui/nodes/SliderPanelNode.cpp
        src/win/ui/nodes/TextBlockNode.cpp
        src/win/ui/nodes/WaveformNode.cpp
        src/win/ui/nodes/TopBarNode.cpp
        src/win/ui/nodes/KnobPanelNode.cpp
        src/win/ui/nodes/FlowDiagramNode.cpp
        src/win/ui/nodes/ModuleCardNode.cpp
        src/win/ui/nodes/ModulePreviewNode.cpp
        src/win/ui/nodes/DropdownSelectorNode.cpp
        src/win/ui/nodes/RoomReverbPreviewNode.cpp
        src/win/ui/nodes/KeyboardNode.cpp
        src/win/ui/VirtualKeyboard.cpp
        src/win/ui/WaveformView.cpp
        src/win/ui/UISkin.cpp
    )
    target_sources(SatoriWinApp PRIVATE "${NUNITO_FONT_RC}")
    target_link_libraries(SatoriWinApp PRIVATE SatoriRealtimeWin d2d1.lib dwrite.lib)
    target_compile_definitions(SatoriWinApp PRIVATE
        $<$<CONFIG:Debug>:SATORI_ENABLE_UI_DEBUG=1>)

    add_executable(SatoriKeyboardSandbox WIN32
        src/win/app/KeyboardSandboxMain.cpp
        src/win/ui/VirtualKeyboard.cpp
        src/win/ui/DebugOverlay.cpp
        src/win/ui/KeyboardKeymap.cpp
    )
    target_sources(SatoriKeyboardSandbox PRIVATE "${NUNITO_FONT_RC}")
    target_include_directories(SatoriKeyboardSandbox PRIVATE src)
    target_link_libraries(SatoriKeyboardSandbox PRIVATE d2d1.lib dwrite.lib)
    target_compile_definitions(SatoriKeyboardSandbox PRIVATE SATORI_ENABLE_UI_DEBUG=1)
    if (MSVC)
        set_target_properties(SatoriKeyboardSandbox PROPERTIES
            EXCLUDE_FROM_DEFAULT_BUILD_RELEASE TRUE)
    endif()

    add_executable(SatoriKnobSandbox WIN32
        src/win/app/KnobTestMain.cpp
        src/win/ui/ParameterKnob.cpp
        src/win/ui/DebugOverlay.cpp
    )
    target_sources(SatoriKnobSandbox PRIVATE "${NUNITO_FONT_RC}")
    target_include_directories(SatoriKnobSandbox PRIVATE src)
    target_link_libraries(SatoriKnobSandbox PRIVATE d2d1.lib dwrite.lib)
    target_compile_definitions(SatoriKnobSandbox PRIVATE SATORI_ENABLE_UI_DEBUG=1)
    if (MSVC)
        set_target_properties(SatoriKnobSandbox PROPERTIES
            EXCLUDE_FROM_DEFAULT_BUILD_RELEASE TRUE)
    endif()
endif()

add_library(Catch2Amalgamated STATIC
    third_party/catch2/catch_amalgamated.cpp
)
target_include_directories(Catch2Amalgamated PUBLIC third_party/catch2)

set(TEST_SOURCES
    tests/main.cpp
    tests/core_tests.cpp
    tests/convolution_reverb_tests.cpp
    tests/string_params_tests.cpp
    tests/voice_manager_tests.cpp
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
