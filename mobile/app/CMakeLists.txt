# SPDX-License-Identifier: GPL-3.0-only
# SLOOP Touch, desktop build (Linux, macOS, Windows) to develop the app quickly. Android goes through
# SloopTouch.jucer (Projucer -> Android Studio): JUCE's CMake API does not support Android.
#
#   cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ [-DJUCE_DIR=/path/to/JUCE]
#   cmake --build build
cmake_minimum_required(VERSION 3.22)
project(SloopTouch VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SLOOP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# JUCE: a local checkout (-DJUCE_DIR=..., or mobile/app/JUCE as the Android build uses) or downloaded once
if(NOT JUCE_DIR AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt)
    set(JUCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/JUCE)
endif()
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- SLOOP's engine: the firmware sources (C) and their generated tables
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(WARNING "SLOOP's engine is built with clang everywhere (the firmware's own tests say the GCC build of "
                    "its preview adapter does not finish): configure with -DCMAKE_C_COMPILER=clang")
endif()
set(SLOOP_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
set(SLOOP_TABLES ${SLOOP_GEN}/felucca_tables.h ${SLOOP_GEN}/felucca_samples.h ${SLOOP_GEN}/felucca_drumkits.h
                 ${SLOOP_GEN}/felucca_fm6.h)
add_custom_command(OUTPUT ${SLOOP_TABLES}
    COMMAND sh ${SLOOP_ROOT}/mobile/tools/gen_engine_tables.sh ${SLOOP_GEN}
    DEPENDS ${SLOOP_ROOT}/tools/gen_tables.py ${SLOOP_ROOT}/tools/gen_samples.py ${SLOOP_ROOT}/tools/gen_drumkits.py
            ${SLOOP_ROOT}/tools/gen_fm6_patches.py
    COMMENT "Generating SLOOP's engine tables")
add_custom_target(sloop_tables DEPENDS ${SLOOP_TABLES})

# the PLAITS engine's DSP: Mutable Instruments' Plaits (MIT), vendored in mobile/third_party/plaits
set(PLAITS_DIR ${SLOOP_ROOT}/mobile/third_party/plaits)
file(GLOB_RECURSE PLAITS_SOURCES ${PLAITS_DIR}/plaits/*.cc ${PLAITS_DIR}/stmlib/*.cc)
add_library(sloop_engine STATIC ${SLOOP_ROOT}/mobile/engine/sloop_engine.c ${SLOOP_ROOT}/mobile/engine/plaits_glue.cc
            ${PLAITS_SOURCES} ${SLOOP_ROOT}/mobile/engine/obxd_glue.cc)
add_dependencies(sloop_engine sloop_tables)
target_include_directories(sloop_engine PRIVATE ${SLOOP_GEN} ${SLOOP_ROOT}/firmware/src ${PLAITS_DIR}
                           ${SLOOP_ROOT}/mobile/third_party/obxd/Engine
                           PUBLIC ${SLOOP_ROOT}/mobile/engine)
# as the firmware's host builds: wrapping arithmetic, no strict aliasing, the firmware's own warnings left alone
target_compile_options(sloop_engine PRIVATE -O2 -fwrapv -fno-strict-aliasing -w)

# ---- the libraries fetched once by mobile/tools/get_deps.sh into mobile/third_party: Ableton Link (header-only,
# with its Asio), Signalsmith Stretch and Linear (header-only)
set(LINK_DIR ${SLOOP_ROOT}/mobile/third_party/link)
set(STRETCH_INCLUDES ${SLOOP_ROOT}/mobile/third_party/signalsmith-stretch/include
                     ${SLOOP_ROOT}/mobile/third_party/signalsmith-linear/include)
if(NOT EXISTS ${LINK_DIR}/include/ableton/Link.hpp
   OR NOT EXISTS ${SLOOP_ROOT}/mobile/third_party/signalsmith-linear/include)
    execute_process(COMMAND sh ${SLOOP_ROOT}/mobile/tools/get_deps.sh COMMAND_ERROR_IS_FATAL ANY)
endif()

# ---- the app
juce_add_gui_app(SloopTouch
    PRODUCT_NAME "SLOOP Touch"
    COMPANY_NAME "SLOOP"
    BUNDLE_ID org.sloop.touch
    VERSION ${PROJECT_VERSION})
juce_generate_juce_header(SloopTouch)
target_sources(SloopTouch PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Views.cpp
    Source/AudioEngine.cpp
    Source/Project.cpp
    Source/ProjectSheet.cpp
    Source/DawProject.cpp
    Source/Midi.cpp
    Source/LinkSync.cpp
    Source/Samples.cpp
    Source/ClipPlayer.cpp
    Source/AudioEditor.cpp
    Source/SelfTest.cpp)
target_compile_definitions(SloopTouch PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SloopTouch,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SloopTouch,JUCE_VERSION>")
# (Link's headers for LinkSync.cpp only; Linux and Android are Link's "Linux" platform)
set_source_files_properties(Source/LinkSync.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${LINK_DIR}/include;${LINK_DIR}/modules/asio-standalone/asio/include"
    COMPILE_DEFINITIONS "LINK_PLATFORM_LINUX=1;ASIO_STANDALONE=1;ASIO_NO_TYPEID=1;ASIO_VERSION_NAMESPACE=sloop_link_asio"
    COMPILE_OPTIONS "-w")
set(STRETCH_FLAGS "")
foreach(dir ${STRETCH_INCLUDES})
    list(APPEND STRETCH_FLAGS -isystem ${dir})              # (their warnings are theirs)
endforeach()
set_source_files_properties(Source/ClipPlayer.cpp PROPERTIES COMPILE_OPTIONS "${STRETCH_FLAGS}")
target_link_libraries(SloopTouch PRIVATE
    sloop_engine
    juce::juce_audio_utils
    juce::juce_gui_basics
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
