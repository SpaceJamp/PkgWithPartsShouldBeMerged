cmake_minimum_required(VERSION 3.24)

# ---------------------------------------------------------------------------
# Default to a Release build
# ---------------------------------------------------------------------------
# Single-config generators (NMake, Ninja, Makefiles) either leave
# CMAKE_BUILD_TYPE empty or - from CMake 4 on - pre-seed the cache with
# "Debug". A careless configure therefore shipped a DEBUG binary linked against
# the debug CRT (MSVCP140D.dll / VCRUNTIME140D.dll / ucrtbased.dll), and such a
# binary only starts on machines that have Visual Studio installed.
#
# This must happen *before* project(), because that is when CMake installs its
# own default. An explicit -DCMAKE_BUILD_TYPE=... from the user is already in the
# cache and is therefore left untouched; multi-config generators such as
# "Visual Studio" ignore CMAKE_BUILD_TYPE completely. To rebuild an existing
# build folder with a different type, use: cmake -U CMAKE_BUILD_TYPE ...
if(NOT DEFINED CACHE{CMAKE_BUILD_TYPE})
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
  message(STATUS "CMAKE_BUILD_TYPE was not given - defaulting to Release")
endif()

project(pkg_merge
        VERSION 1.0.0
        DESCRIPTION "Merge split PS4 PKG pieces back into a single PKG"
        LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  message(WARNING
    "Building '${CMAKE_BUILD_TYPE}' with a single-config generator. "
    "The binary that gets released should be Release; a Debug build links "
    "against the debug CRT and will not run without Visual Studio installed.")
endif()

# Static CRT so the exe is a single self-contained file with no redistributable
# dependency (/MT for Release, /MTd for Debug).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PKG_MERGE_WERROR "Treat compiler warnings as errors" OFF)

# Embedded in the Windows version resource.
set(PKG_MERGE_COPYRIGHT "Copyright (c) 2026 SpaceJamp - GPL-3.0-only."
    CACHE STRING "Copyright string embedded in the Windows version resource")

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
# The implementation is being written from docs/SPEC.md. Add the new source
# files here; see AGENTS.md for the rules that apply.
#
# The previous implementation is deliberately absent from this tree. It is
# all-rights-reserved upstream-derived code; it survives only in the git history
# and must not be restored or copied from. Do not name this variable after it.
set(PKG_MERGE_SOURCES
    src/main.cpp
    src/pkgmerge/app.cpp
    src/pkgmerge/cli.cpp
    src/pkgmerge/fileio.cpp
    src/pkgmerge/merge.cpp
    src/pkgmerge/platform.cpp
    src/pkgmerge/scan.cpp
    src/pkgmerge/term.cpp
    src/pkgmerge/text.cpp
    )
add_executable(pkg_merge ${PKG_MERGE_SOURCES})

target_compile_features(pkg_merge PRIVATE cxx_std_20)
target_compile_definitions(pkg_merge PRIVATE PKG_MERGE_VERSION="${PROJECT_VERSION}")
# The repository root keeps "docs/SPEC.md"-style paths addressable; src is what
# makes #include "pkgmerge/..." resolve from inside the tree.
target_include_directories(pkg_merge PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(WIN32)
  target_compile_definitions(pkg_merge PRIVATE UNICODE _UNICODE)
endif()

# ---------------------------------------------------------------------------
# Warnings and hardening
# ---------------------------------------------------------------------------
if(MSVC)
  target_compile_options(pkg_merge PRIVATE
    /W4
    /permissive-
    /utf-8
    /Zc:__cplusplus
    /Zc:preprocessor
    /MP)
  # /DYNAMICBASE (ASLR) and /NXCOMPAT (DEP) are on by default in modern link.exe;
  # /guard:cf is not, and it is what makes Control Flow Guard actually enforce.
  target_link_options(pkg_merge PRIVATE
    /DYNAMICBASE
    /NXCOMPAT
    /guard:cf
    /CETCOMPAT      # ignored below for x86, where shadow stacks do not exist
    # Without this the manifest that reaches the executable is the CRT's own, and
    # longPathAware is lost with it: every path over 260 characters then fails
    # (spec sections 15 and 17.8).
    "/MANIFESTINPUT:${CMAKE_CURRENT_BINARY_DIR}/app.manifest")
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    # CET shadow stack is x64 only; HIGHENTROPYVA needs a >4 GiB address space.
    get_target_property(_opts pkg_merge LINK_OPTIONS)
    list(REMOVE_ITEM _opts "/CETCOMPAT")
    set_property(TARGET pkg_merge PROPERTY LINK_OPTIONS "${_opts}")
  endif()
  if(PKG_MERGE_WERROR)
    target_compile_options(pkg_merge PRIVATE /WX)
  endif()
else()
  target_compile_options(pkg_merge PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # GNU ld hardening. Apple's ld does not understand -z relro / -z now and
    # fails the link with "unknown options: -z -z".
    target_link_options(pkg_merge PRIVATE -Wl,-z,relro -Wl,-z,now)
  elseif(APPLE)
    target_link_options(pkg_merge PRIVATE -Wl,-bind_at_load)
  endif()
  if(PKG_MERGE_WERROR)
    target_compile_options(pkg_merge PRIVATE -Werror)
  endif()
endif()

# ---------------------------------------------------------------------------
# Folder dialog
# ---------------------------------------------------------------------------
# Windows: the Win32 IFileDialog, so there is no third-party code.
# Linux:   zenity or kdialog, located at run time - not a build dependency.
#
# The project links no third-party code on any platform.
if(WIN32)
  target_link_libraries(pkg_merge PRIVATE ole32 shell32 uuid)
endif()

# ---------------------------------------------------------------------------
# Resources (version info + application manifest)
# ---------------------------------------------------------------------------
if(WIN32)
  enable_language(RC)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/pkg_merge.rc.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/pkg_merge.rc" @ONLY)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/app.manifest" @ONLY)
  target_sources(pkg_merge PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/pkg_merge.rc")
endif()

# ---------------------------------------------------------------------------
# Install / package
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS pkg_merge RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/SpaceJamp/PkgWithPartsShouldBeMerged")
if(WIN32)
  set(CPACK_GENERATOR ZIP)
endif()
include(CPack)

message(STATUS "pkg-merge ${PROJECT_VERSION}: ${CMAKE_BUILD_TYPE} ${CMAKE_SYSTEM_PROCESSOR}")
