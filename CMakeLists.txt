cmake_minimum_required(VERSION 3.24)
project(JlMol VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_MACOSX_RPATH ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(JLMOL_ENABLE_JULIA "Build the embedded Julia runtime" ON)
option(JLMOL_BUILD_TESTS "Build native tests" ON)
option(JLMOL_USE_CHEMFILES "Use Chemfiles for molecular file import" ON)
option(JLMOL_USE_SYSTEM_CHEMFILES "Use an installed Chemfiles instead of the patched bundled version" OFF)
option(JLMOL_PACKAGE_DESKTOP "Configure a Windows MSI or Linux portable archive" OFF)

if(JLMOL_PACKAGE_DESKTOP AND (CMAKE_CROSSCOMPILING OR (NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")))
    message(FATAL_ERROR "Build the Windows or Linux desktop package on its target OS.")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

if(APPLE AND EXISTS "/opt/homebrew/opt/zlib/lib/libz.dylib")
    set(ZLIB_ROOT "/opt/homebrew/opt/zlib" CACHE PATH "Homebrew zlib root" FORCE)
    set(ZLIB_LIBRARY "/opt/homebrew/opt/zlib/lib/libz.dylib" CACHE FILEPATH "zlib library" FORCE)
    set(ZLIB_INCLUDE_DIR "/opt/homebrew/opt/zlib/include" CACHE PATH "zlib include directory" FORCE)
endif()

if(JLMOL_USE_CHEMFILES)
    if(JLMOL_USE_SYSTEM_CHEMFILES)
        find_package(chemfiles 0.10 CONFIG REQUIRED)
    else()
        include(FetchContent)
        find_package(Git REQUIRED)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies statically" FORCE)
        set(CHFL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(CHFL_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
        set(CHFL_USE_WARNINGS OFF CACHE BOOL "" FORCE)
        # Standalone Windows and Linux packages use the compressors bundled with Chemfiles.
        set(jlmol_system_compression ON)
        if(WIN32 OR JLMOL_PACKAGE_DESKTOP)
            set(jlmol_system_compression OFF)
        endif()
        set(CHFL_SYSTEM_ZLIB ${jlmol_system_compression} CACHE BOOL "" FORCE)
        set(CHFL_SYSTEM_LZMA ${jlmol_system_compression} CACHE BOOL "" FORCE)
        set(CHFL_SYSTEM_BZIP2 ${jlmol_system_compression} CACHE BOOL "" FORCE)
        # Chemfiles 0.10 supports older CMake releases; this keeps CMake 4 compatible.
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
        FetchContent_Declare(chemfiles
            GIT_REPOSITORY https://github.com/chemfiles/chemfiles.git
            GIT_TAG 843e5949db829dcf76bac1ca3521cc8e10f9ea4e
            PATCH_COMMAND
                "${CMAKE_COMMAND}"
                "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
                "-DSOURCE_DIR=<SOURCE_DIR>"
                "-DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/chemfiles-rotated-cell.patch"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyDependencyPatch.cmake"
        )
        FetchContent_MakeAvailable(chemfiles)
        # Chemfiles enables glibc large-file extensions unconditionally. macOS
        # does not provide off64_t, so remove those two private definitions while
        # retaining _FILE_OFFSET_BITS=64 and the system zlib implementation.
        if(APPLE AND TARGET chemfiles_objects)
            get_target_property(_jlmol_chemfiles_definitions chemfiles_objects COMPILE_DEFINITIONS)
            if(_jlmol_chemfiles_definitions)
                list(FILTER _jlmol_chemfiles_definitions EXCLUDE REGEX "_LARGEFILE64_SOURCE|_LFS64_LARGEFILE")
                set_property(TARGET chemfiles_objects PROPERTY COMPILE_DEFINITIONS "${_jlmol_chemfiles_definitions}")
                target_compile_definitions(chemfiles_objects PRIVATE
                    gzopen64=gzopen
                    gzseek64=gzseek
                    z_off64_t=z_off_t
                )
            endif()
        endif()
    endif()
endif()

set(JLMOL_CORE_SOURCES
    native/core/Analysis.cpp
    native/core/MmcifMetadata.cpp
    native/core/Molecule.cpp
    native/core/MolecularScene.cpp
    native/core/Trajectory.cpp
    native/bridge/JlMolAPI.cpp
)

add_library(jlmol_core SHARED ${JLMOL_CORE_SOURCES})
target_include_directories(jlmol_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/native)
target_compile_definitions(jlmol_core PRIVATE JLMOL_CORE_BUILD)
if(TARGET chemfiles)
    target_link_libraries(jlmol_core PRIVATE chemfiles)
    target_compile_definitions(jlmol_core PRIVATE JLMOL_HAS_CHEMFILES=1)
endif()
set_target_properties(jlmol_core PROPERTIES OUTPUT_NAME jlmol_core)
if(WIN32)
    set_target_properties(jlmol_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_compile_definitions(jlmol_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

set(JLMOL_APP_SOURCES
    native/app/ApplicationPaths.cpp
    native/app/main.cpp
    native/commands/CommandDispatcher.cpp
    native/gui/ContactMapWidget.cpp
    native/gui/FigureExport.cpp
    native/gui/JuliaConsole.cpp
    native/gui/MainWindow.cpp
    native/gui/MolecularViewport.cpp
    native/gui/PerformanceBenchmark.cpp
    native/gui/ScriptEditor.cpp
    native/gui/TrajectoryAnalysisPanel.cpp
    native/gui/TrajectoryPanel.cpp
    native/gui/WorkspaceFile.cpp
    native/renderer/CartoonGeometry.cpp
    native/scripting/JuliaRuntime.cpp
)

if(WIN32)
    qt_add_executable(jlmol WIN32 ${JLMOL_APP_SOURCES})
    target_compile_definitions(jlmol PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(jlmol PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
elseif(APPLE)
    add_executable(jlmol MACOSX_BUNDLE ${JLMOL_APP_SOURCES})
else()
    qt_add_executable(jlmol ${JLMOL_APP_SOURCES})
endif()
target_include_directories(jlmol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/native)
target_link_libraries(jlmol PRIVATE
    jlmol_core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
)
target_compile_definitions(jlmol PRIVATE
    JLMOL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    JLMOL_CORE_LIBRARY_PATH="$<TARGET_FILE:jlmol_core>"
    JLMOL_CORE_LIBRARY_NAME="$<TARGET_FILE_NAME:jlmol_core>"
)

if(JLMOL_ENABLE_JULIA)
    find_program(JULIA_EXECUTABLE julia)
    if(JULIA_EXECUTABLE)
        execute_process(
            COMMAND "${JULIA_EXECUTABLE}" --startup-file=no -e "print(normpath(joinpath(Sys.BINDIR, \"..\")))"
            OUTPUT_VARIABLE JULIA_ROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE JULIA_DISCOVERY_RESULT
        )
        if(JULIA_DISCOVERY_RESULT STREQUAL "0")
            file(TO_CMAKE_PATH "${JULIA_ROOT}" JULIA_ROOT)
        endif()
    endif()
    set(jlmol_julia_library "${JULIA_ROOT}/lib/libjulia${CMAKE_SHARED_LIBRARY_SUFFIX}")
    if(WIN32)
        # The official Windows runtime ships separate import libraries and DLLs.
        set(jlmol_julia_library "${JULIA_ROOT}/lib/libjulia.dll.a")
        set(jlmol_julia_runtime "${JULIA_ROOT}/bin/libjulia.dll")
    else()
        set(jlmol_julia_runtime "${jlmol_julia_library}")
    endif()
    if(JULIA_DISCOVERY_RESULT STREQUAL "0" AND EXISTS "${JULIA_ROOT}/include/julia/julia.h"
       AND EXISTS "${jlmol_julia_library}" AND EXISTS "${jlmol_julia_runtime}")
        target_include_directories(jlmol PRIVATE "${JULIA_ROOT}/include/julia")
        target_link_libraries(jlmol PRIVATE "${jlmol_julia_library}")
        if(WIN32 AND EXISTS "${JULIA_ROOT}/lib/libopenlibm.dll.a")
            target_link_libraries(jlmol PRIVATE "${JULIA_ROOT}/lib/libopenlibm.dll.a")
        endif()
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
        set(JLMOL_EMBEDDED_JULIA TRUE)
    else()
        message(WARNING "Julia development runtime not found; building a disabled console")
    endif()
endif()

function(jlmol_install_project destination)
    install(FILES Project.toml LICENSE DESTINATION "${destination}" COMPONENT JlMolDesktop)
    install(DIRECTORY src examples DESTINATION "${destination}" COMPONENT JlMolDesktop)
    install(FILES resources/1CRN.pdb resources/README.md DESTINATION "${destination}/resources" COMPONENT JlMolDesktop)
    install(FILES docs/third_party_notices.md DESTINATION "${destination}/licenses"
        RENAME README.md COMPONENT JlMolDesktop)
    install(FILES test/scientific_reference.jl DESTINATION "${destination}/test" COMPONENT JlMolDesktop)
    install(FILES test/data/crambin_reference.toml test/data/crambin_reference.trr test/data/crambin_missing_positions.trr
        test/data/water.xyz test/data/water_trajectory.xyz test/data/water_analysis.xyz
        DESTINATION "${destination}/test/data" COMPONENT JlMolDesktop)
    if(DEFINED chemfiles_SOURCE_DIR AND EXISTS "${chemfiles_SOURCE_DIR}/LICENSE")
        install(FILES "${chemfiles_SOURCE_DIR}/LICENSE" DESTINATION "${destination}/licenses"
            RENAME Chemfiles.txt COMPONENT JlMolDesktop)
    endif()
    if(DEFINED chemfiles_BINARY_DIR)
        foreach(notice IN ITEMS bzip2/LICENSE fmt/LICENSE.rst gemmi/include/gemmi/third_party/tao/LICENSE
                lzma/COPYING mmtf-cpp/LICENSE molfiles/LICENSE pugixml/LICENSE.md tng/COPYING toml11/LICENSE zlib/README)
            set(notice_path "${chemfiles_BINARY_DIR}/external/${notice}")
            if(EXISTS "${notice_path}")
                get_filename_component(notice_dir "${notice}" DIRECTORY)
                install(FILES "${notice_path}" DESTINATION "${destination}/licenses/${notice_dir}" COMPONENT JlMolDesktop)
            endif()
        endforeach()
    endif()
endfunction()

if(APPLE)
    set_target_properties(jlmol PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "JlMol"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.jlmol.desktop"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
    if(JLMOL_EMBEDDED_JULIA)
        set(jlmol_contents "jlmol.app/Contents")
        install(TARGETS jlmol BUNDLE DESTINATION . COMPONENT JlMolDesktop)
        install(TARGETS jlmol_core LIBRARY DESTINATION "${jlmol_contents}/Frameworks" COMPONENT JlMolDesktop)
        install(DIRECTORY "${JULIA_ROOT}/" DESTINATION "${jlmol_contents}/Resources/Julia"
            USE_SOURCE_PERMISSIONS COMPONENT JlMolDesktop
            PATTERN "*.dSYM" EXCLUDE PATTERN "libccalllazy*.dylib" EXCLUDE)
        jlmol_install_project("${jlmol_contents}/Resources/jlmol")
    endif()
endif()

if(JLMOL_PACKAGE_DESKTOP)
    include(cmake/PackageDesktop.cmake)
endif()

include(CTest)
if(BUILD_TESTING AND JLMOL_BUILD_TESTS)
    # These test executables use assert to check operations, including in a release build.
    if(MSVC)
        set(jlmol_test_asserts /UNDEBUG)
    else()
        set(jlmol_test_asserts -UNDEBUG)
    endif()
    add_executable(jlmol_core_tests test/native_core_tests.cpp)
    target_compile_options(jlmol_core_tests PRIVATE ${jlmol_test_asserts})
    target_link_libraries(jlmol_core_tests PRIVATE jlmol_core)
    add_test(NAME native_core COMMAND jlmol_core_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/test/data/mini.pdb"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/1CRN.pdb"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/data/water.xyz"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/data/mini.cif"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/data/secondary.mmcif"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/data/alternate.pdb")
    if(TARGET chemfiles)
        add_executable(jlmol_trajectory_tests test/trajectory_tests.cpp)
        target_compile_options(jlmol_trajectory_tests PRIVATE ${jlmol_test_asserts})
        target_link_libraries(jlmol_trajectory_tests PRIVATE jlmol_core chemfiles)
        if(NOT JLMOL_USE_SYSTEM_CHEMFILES)
            target_compile_definitions(jlmol_trajectory_tests PRIVATE JLMOL_TEST_ROTATED_CELL_IMPORT=1)
        endif()
        add_test(NAME native_trajectory COMMAND jlmol_trajectory_tests)
        if(JLMOL_EMBEDDED_JULIA)
            add_test(NAME scientific_reference COMMAND jlmol --scientific-test)
            set_tests_properties(scientific_reference PROPERTIES
                ENVIRONMENT "JULIA_DEPOT_PATH=${CMAKE_BINARY_DIR}/julia-depot"
                TIMEOUT 90)
            if(WIN32)
                set_tests_properties(scientific_reference PROPERTIES
                    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${JULIA_ROOT}/bin")
            endif()
        endif()
    endif()
endif()

