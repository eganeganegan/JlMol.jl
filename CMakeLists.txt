cmake_minimum_required(VERSION 3.24)
project(JlMol VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_MACOSX_RPATH ON)

option(JLMOL_ENABLE_JULIA "Build the embedded Julia runtime" ON)
option(JLMOL_BUILD_TESTS "Build native tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

set(JLMOL_CORE_SOURCES
    native/core/Molecule.cpp
    native/core/MolecularScene.cpp
    native/bridge/JlMolAPI.cpp
)

add_library(jlmol_core SHARED ${JLMOL_CORE_SOURCES})
target_include_directories(jlmol_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/native)
target_compile_definitions(jlmol_core PRIVATE JLMOL_CORE_BUILD)
set_target_properties(jlmol_core PROPERTIES OUTPUT_NAME jlmol_core)

set(JLMOL_APP_SOURCES
    native/app/main.cpp
    native/commands/CommandDispatcher.cpp
    native/gui/JuliaConsole.cpp
    native/gui/MainWindow.cpp
    native/gui/MolecularViewport.cpp
    native/scripting/JuliaRuntime.cpp
)

add_executable(jlmol MACOSX_BUNDLE ${JLMOL_APP_SOURCES})
target_include_directories(jlmol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/native)
target_link_libraries(jlmol PRIVATE
    jlmol_core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
)
target_compile_definitions(jlmol PRIVATE
    JLMOL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    JLMOL_CORE_LIBRARY_PATH="$<TARGET_FILE:jlmol_core>"
)

if(JLMOL_ENABLE_JULIA)
    find_program(JULIA_EXECUTABLE julia)
    if(JULIA_EXECUTABLE)
        execute_process(
            COMMAND "${JULIA_EXECUTABLE}" -e "print(normpath(joinpath(Sys.BINDIR, \"..\")))"
            OUTPUT_VARIABLE JULIA_ROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE JULIA_DISCOVERY_RESULT
        )
    endif()
    if(JULIA_DISCOVERY_RESULT EQUAL 0
       AND EXISTS "${JULIA_ROOT}/include/julia/julia.h"
       AND EXISTS "${JULIA_ROOT}/lib/libjulia${CMAKE_SHARED_LIBRARY_SUFFIX}")
        target_include_directories(jlmol PRIVATE "${JULIA_ROOT}/include/julia")
        target_link_libraries(jlmol PRIVATE "${JULIA_ROOT}/lib/libjulia${CMAKE_SHARED_LIBRARY_SUFFIX}")
        target_compile_definitions(jlmol PRIVATE
            JLMOL_HAS_JULIA=1
            JLMOL_JULIA_BINDIR="${JULIA_ROOT}/bin"
        )
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(jlmol PRIVATE -Wno-nullability-completeness)
        endif()
        set_property(TARGET jlmol APPEND PROPERTY BUILD_RPATH
            "${JULIA_ROOT}/lib" "${JULIA_ROOT}/lib/julia")
        message(STATUS "Embedded Julia enabled from ${JULIA_ROOT}")
    else()
        message(WARNING "Julia development runtime not found; building a disabled console")
    endif()
endif()

if(APPLE)
    set_target_properties(jlmol PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "JlMol"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.jlmol.desktop"
    )
endif()

include(CTest)
if(BUILD_TESTING AND JLMOL_BUILD_TESTS)
    add_executable(jlmol_core_tests test/native_core_tests.cpp)
    target_link_libraries(jlmol_core_tests PRIVATE jlmol_core)
    add_test(NAME native_core COMMAND jlmol_core_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/test/data/mini.pdb"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/1CRN.pdb")
endif()

