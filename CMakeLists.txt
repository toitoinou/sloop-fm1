# SPDX-License-Identifier: GPL-3.0-only
# SLOOP Touch, the desktop build (Linux, macOS, Windows): the app, to develop it quickly, and its tests. Android
# goes through app/SloopTouch.jucer (Projucer -> Android Studio): JUCE's CMake API does not support Android.
#
#   cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ [-DJUCE_DIR=/path/to/JUCE]
#   cmake --build build
#   ctest --test-dir build            (the unit tests; the app's own --selftest is the end-to-end one)
cmake_minimum_required(VERSION 3.22)
project(SloopTouch VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# JUCE: a local checkout (-DJUCE_DIR=..., or app/JUCE as the Android build uses) or downloaded once
if(NOT JUCE_DIR AND EXISTS ${ROOT}/app/JUCE/CMakeLists.txt)
    set(JUCE_DIR ${ROOT}/app/JUCE)
endif()
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- the libraries fetched once by tools/get_deps.sh into third_party: Ableton Link (header-only, with its Asio),
# Signalsmith Stretch and Linear (header-only)
set(LINK_DIR ${ROOT}/third_party/link)
set(STRETCH_INCLUDES ${ROOT}/third_party/signalsmith-stretch/include ${ROOT}/third_party/signalsmith-linear/include)
if(NOT EXISTS ${LINK_DIR}/include/ableton/Link.hpp OR NOT EXISTS ${ROOT}/third_party/signalsmith-linear/include)
    execute_process(COMMAND sh ${ROOT}/tools/get_deps.sh COMMAND_ERROR_IS_FATAL ANY)
endif()

# ---- the engine (C): SLOOP's DSP (engine/core) through engine/host.c, its generated tables, PLAITS and OBXD
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(WARNING "The engine is built with clang everywhere (GCC takes too long on its one translation unit): "
                    "configure with -DCMAKE_C_COMPILER=clang")
endif()
set(ENGINE_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
set(ENGINE_TABLES ${ENGINE_GEN}/felucca_tables.h ${ENGINE_GEN}/felucca_samples.h ${ENGINE_GEN}/felucca_drumkits.h
                  ${ENGINE_GEN}/felucca_fm6.h)
add_custom_command(OUTPUT ${ENGINE_TABLES}
    COMMAND sh ${ROOT}/tools/gen_engine_tables.sh ${ENGINE_GEN}
    DEPENDS ${ROOT}/tools/gen_tables.py ${ROOT}/tools/gen_samples.py ${ROOT}/tools/gen_drumkits.py
            ${ROOT}/tools/gen_fm6_patches.py
    COMMENT "Generating the engine's tables")
add_custom_target(engine_tables DEPENDS ${ENGINE_TABLES})
set(PLAITS_DIR ${ROOT}/third_party/plaits)
file(GLOB_RECURSE PLAITS_SOURCES ${PLAITS_DIR}/plaits/*.cc ${PLAITS_DIR}/stmlib/*.cc)
add_library(sloop_engine STATIC ${ROOT}/engine/sloop_engine.c ${ROOT}/engine/plaits_glue.cc ${ROOT}/engine/obxd_glue.cc
            ${PLAITS_SOURCES})
add_dependencies(sloop_engine engine_tables)
target_include_directories(sloop_engine PRIVATE ${ENGINE_GEN} ${ROOT}/engine/core ${PLAITS_DIR} ${ROOT}/third_party/obxd/Engine
                           PUBLIC ${ROOT}/engine)
# wrapping arithmetic, no strict aliasing (as SLOOP's own builds), its warnings left alone
target_compile_options(sloop_engine PRIVATE -O2 -fwrapv -fno-strict-aliasing -w)

# ---- the app's sources: everything under app/Source but its entry point (shared with the tests)
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS ${ROOT}/app/Source/*.cpp)
list(REMOVE_ITEM APP_SOURCES ${ROOT}/app/Source/Main.cpp)
# (Link's headers for LinkSync.cpp only; Linux and Android are Link's "Linux" platform)
set_source_files_properties(${ROOT}/app/Source/audio/LinkSync.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${LINK_DIR}/include;${LINK_DIR}/modules/asio-standalone/asio/include"
    COMPILE_DEFINITIONS "LINK_PLATFORM_LINUX=1;ASIO_STANDALONE=1;ASIO_NO_TYPEID=1;ASIO_VERSION_NAMESPACE=sloop_link_asio"
    COMPILE_OPTIONS "-w")
set(STRETCH_FLAGS "")
foreach(dir ${STRETCH_INCLUDES})
    list(APPEND STRETCH_FLAGS -isystem ${dir})               # (their warnings are theirs)
endforeach()
set_source_files_properties(${ROOT}/app/Source/audio/ClipPlayer.cpp PROPERTIES COMPILE_OPTIONS "${STRETCH_FLAGS}")
set(APP_DEFINITIONS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)

juce_add_gui_app(SloopTouch
    PRODUCT_NAME "SLOOP Touch"
    COMPANY_NAME "SLOOP"
    BUNDLE_ID org.sloop.touch
    VERSION ${PROJECT_VERSION})
juce_generate_juce_header(SloopTouch)
target_sources(SloopTouch PRIVATE ${ROOT}/app/Source/Main.cpp ${APP_SOURCES})
target_include_directories(SloopTouch PRIVATE ${ROOT}/app/Source)
target_compile_definitions(SloopTouch PRIVATE ${APP_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SloopTouch,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SloopTouch,JUCE_VERSION>")
target_link_libraries(SloopTouch PRIVATE
    sloop_engine
    juce::juce_audio_utils
    juce::juce_gui_basics
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- the unit tests: the app's rules on plain data (the scheduler, the clips, undo, projects), no device, no screen
enable_testing()
juce_add_console_app(SloopTests PRODUCT_NAME "SloopTests")
juce_generate_juce_header(SloopTests)
target_sources(SloopTests PRIVATE
    ${ROOT}/tests/Main.cpp
    ${ROOT}/tests/SchedulerTests.cpp
    ${ROOT}/tests/ModelTests.cpp
    ${ROOT}/tests/ProjectTests.cpp
    ${ROOT}/app/Source/audio/Scheduler.cpp
    ${ROOT}/app/Source/model/Project.cpp
    ${ROOT}/app/Source/model/Bundle.cpp
    ${ROOT}/app/Source/model/Samples.cpp
    ${ROOT}/app/Source/model/DawProject.cpp)
target_include_directories(SloopTests PRIVATE ${ROOT}/app/Source)
target_compile_definitions(SloopTests PRIVATE ${APP_DEFINITIONS})
target_link_libraries(SloopTests PRIVATE
    sloop_engine
    juce::juce_audio_formats
    juce::juce_data_structures
    juce::juce_graphics
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
add_test(NAME unit COMMAND SloopTests)
