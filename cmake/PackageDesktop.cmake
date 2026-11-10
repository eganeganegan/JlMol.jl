if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "The Windows installer requires an MSVC-compatible compiler and Qt kit.")
endif()
if(NOT JLMOL_EMBEDDED_JULIA OR NOT JLMOL_USE_CHEMFILES OR JLMOL_USE_SYSTEM_CHEMFILES)
    message(FATAL_ERROR "Desktop packages require embedded Julia and the bundled Chemfiles build.")
endif()

include(GNUInstallDirs)
if(NOT CMAKE_INSTALL_BINDIR STREQUAL "bin" OR NOT CMAKE_INSTALL_LIBDIR STREQUAL "lib")
    message(FATAL_ERROR "The portable layout uses bin and lib. Set CMAKE_INSTALL_BINDIR=bin and CMAKE_INSTALL_LIBDIR=lib.")
endif()

install(TARGETS jlmol jlmol_core
    RUNTIME DESTINATION bin COMPONENT JlMolDesktop
    LIBRARY DESTINATION lib COMPONENT JlMolDesktop)
jlmol_install_project("share/jlmol")

if(WIN32)
    # Julia's DLL must sit beside jlmol.exe before the Windows loader starts the process.
    install(DIRECTORY "${JULIA_ROOT}/" DESTINATION . USE_SOURCE_PERMISSIONS COMPONENT JlMolDesktop
        PATTERN "*.pdb" EXCLUDE)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT JlMolDesktop)
    include(InstallRequiredSystemLibraries)
    qt_generate_deploy_app_script(TARGET jlmol OUTPUT_SCRIPT deploy_desktop NO_COMPILER_RUNTIME NO_TRANSLATIONS)

    set(CPACK_GENERATOR WIX)
    set(CPACK_WIX_UPGRADE_GUID "CEB5A879-433C-44B5-83FC-3A2798669D13")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "JlMol")
    set(CPACK_PACKAGE_EXECUTABLES "jlmol" "JlMol")
else()
    install(DIRECTORY "${JULIA_ROOT}/" DESTINATION lib/jlmol/Julia USE_SOURCE_PERMISSIONS COMPONENT JlMolDesktop)
    set_target_properties(jlmol PROPERTIES INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/../lib/jlmol/Julia/lib")
    set_target_properties(jlmol_core PROPERTIES INSTALL_RPATH "$ORIGIN")
    # The core and Julia already have install rules and must retain their own layouts.
    qt_generate_deploy_app_script(TARGET jlmol OUTPUT_SCRIPT deploy_desktop NO_TRANSLATIONS
        PRE_EXCLUDE_REGEXES "^libjlmol_core[.]so$" "^libjulia[.]so$" "^libjulia[.]so[.]")
    set(CPACK_GENERATOR TGZ)
endif()
install(SCRIPT "${deploy_desktop}" COMPONENT JlMolDesktop)

set(CPACK_PACKAGE_NAME "JlMol")
set(CPACK_PACKAGE_VENDOR "JlMol contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Julia-powered molecular visualization")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
configure_file("${PROJECT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_BINARY_DIR}/JlMol-License.txt" COPYONLY)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/JlMol-License.txt")
set(CPACK_PACKAGE_FILE_NAME "JlMol-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
# Package only the app's install rules, including the generated Qt deployment step.
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};JlMolDesktop;/")
include(CPack)
