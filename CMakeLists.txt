# SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# Version 3.24 needed for FetchContent OVERRIDE_FIND_PACKAGE
cmake_minimum_required(VERSION 3.24)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(APPLE)
    list(APPEND ADDITIONAL_LANGUAGES OBJC)
    # Starting with 15.4, Rosetta 2 has support for all the necessary instruction sets.
    set(CMAKE_OSX_DEPLOYMENT_TARGET 15.4 CACHE STRING "")
endif()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

project(shadPS4QtLauncher CXX C ASM ${ADDITIONAL_LANGUAGES})

# Forcing PIE makes sure that the base address is high enough so that it doesn't clash with the PS4 memory.
if(UNIX AND NOT APPLE)
    set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

    # check PIE support at link time
    include(CheckPIESupported)
    check_pie_supported(OUTPUT_VARIABLE pie_check LANGUAGES C CXX)
    if(NOT CMAKE_C_LINK_PIE_SUPPORTED OR NOT CMAKE_CXX_LINK_PIE_SUPPORTED)
        message(WARNING "PIE is not supported at link time: ${pie_check}")
    endif()
endif()

option(ENABLE_UPDATER "Enables the options to updater" ON)

# First, determine whether to use CMAKE_OSX_ARCHITECTURES or CMAKE_SYSTEM_PROCESSOR.
if (APPLE AND CMAKE_OSX_ARCHITECTURES)
    set(BASE_ARCHITECTURE "${CMAKE_OSX_ARCHITECTURES}")
elseif (CMAKE_SYSTEM_PROCESSOR)
    set(BASE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
else()
    set(BASE_ARCHITECTURE "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

# Next, match common architecture strings down to a known common value.
if (BASE_ARCHITECTURE MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
    set(ARCHITECTURE "x86_64")
elseif (BASE_ARCHITECTURE MATCHES "(aarch64)|(AARCH64)|(arm64)|(ARM64)")
    set(ARCHITECTURE "arm64")
else()
    message(FATAL_ERROR "Unsupported CPU architecture: ${BASE_ARCHITECTURE}")
endif()

if (ARCHITECTURE STREQUAL "x86_64")
    # Target x86-64-v3 CPU architecture as this is a good balance between supporting performance critical
    # instructions like AVX2 and maintaining support for older CPUs.
    add_compile_options(-march=x86-64-v3)
endif()

if (APPLE AND ARCHITECTURE STREQUAL "x86_64" AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
    # Exclude ARM homebrew path to avoid conflicts when cross compiling.
    list(APPEND CMAKE_IGNORE_PREFIX_PATH "/opt/homebrew")

    # Need to reconfigure pkg-config to use the right architecture library paths.
    # It's not ideal to override these but otherwise the build breaks just by having pkg-config installed.
    set(ENV{PKG_CONFIG_DIR} "")
    set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig:${CMAKE_SYSROOT}/usr/local/lib/pkgconfig:${CMAKE_SYSROOT}/usr/local/share/pkgconfig")
    set(ENV{PKG_CONFIG_SYSROOT_DIR} ${CMAKE_SYSROOT})
endif()

# This function should be passed a list of all files in a target. It will automatically generate file groups
# following the directory hierarchy, so that the layout of the files in IDEs matches the one in the filesystem.
function(create_target_directory_groups target_name)

    # Place any files that aren't in the source list in a separate group so that they don't get in the way.
    source_group("Other Files" REGULAR_EXPRESSION ".")

    get_target_property(target_sources "${target_name}" SOURCES)

    foreach(file_name IN LISTS target_sources)
        get_filename_component(dir_name "${file_name}" PATH)
        # Group names use '\' as a separator even though the entire rest of CMake uses '/'...
        string(REPLACE "/" "\\" group_name "${dir_name}")
        source_group("${group_name}" FILES "${file_name}")
    endforeach()
endfunction()

# Setup a custom clang-format target (if clang-format can be found) that will run
# against all the src files. This should be used before making a pull request.
if (CLANG_FORMAT)
    set(SRCS ${PROJECT_SOURCE_DIR}/src)
    set(CCOMMENT "Running clang format against all the .h and .cpp files in src/")
    if (WIN32)
        add_custom_target(clang-format
            COMMAND powershell.exe -Command "Get-ChildItem '${SRCS}/*' -Include *.cpp,*.h,*.mm -Recurse | Foreach {&'${CLANG_FORMAT}' -i $_.fullname}"
            COMMENT ${CCOMMENT})
    else()
        add_custom_target(clang-format
            COMMAND find ${SRCS} -iname *.h -o -iname *.cpp -o -iname *.mm | xargs ${CLANG_FORMAT} -i
            COMMENT ${CCOMMENT})
    endif()
    unset(SRCS)
    unset(CCOMMENT)
endif()

# generate git revision information
include("${CMAKE_CURRENT_SOURCE_DIR}/externals/cmake-modules/GetGitRevisionDescription.cmake")
get_git_head_revision(GIT_REF_SPEC GIT_REV)
git_describe(GIT_DESC --always --long --dirty)
git_branch_name(GIT_BRANCH)
string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M:%S")

message("start git things")

execute_process(
  COMMAND git rev-list --count @
  OUTPUT_VARIABLE GIT_REV_LIST_NUMBER
  RESULT_VARIABLE GIT_REV_LIST_RESULT
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Try to get the upstream remote and branch
message("check for remote and branch")
execute_process(
  COMMAND git rev-parse --abbrev-ref --symbolic-full-name @{u}
  OUTPUT_VARIABLE GIT_REMOTE_NAME
  RESULT_VARIABLE GIT_REMOTE_RESULT
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# If there's no upstream set or the command failed, check remote.pushDefault
if (GIT_REMOTE_RESULT OR GIT_REMOTE_NAME STREQUAL "")
  message(STATUS "check default push")
  execute_process(
    COMMAND git config --get remote.pushDefault
    OUTPUT_VARIABLE GIT_REMOTE_NAME
    RESULT_VARIABLE GIT_REMOTE_RESULT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  message(STATUS "got remote: ${GIT_REMOTE_NAME}")
endif()

# If running in GitHub Actions and the above fails
if (GIT_REMOTE_RESULT OR GIT_REMOTE_NAME STREQUAL "")
  message(STATUS "check github")
  set(GIT_REMOTE_NAME "origin")

  # Retrieve environment variables
  if (DEFINED ENV{GITHUB_HEAD_REF} AND NOT "$ENV{GITHUB_HEAD_REF}" STREQUAL "")
    message(STATUS "github head ref: $ENV{GITHUB_HEAD_REF}")
    set(GITHUB_HEAD_REF "$ENV{GITHUB_HEAD_REF}")
  else()
    set(GITHUB_HEAD_REF "")
  endif()

  if (DEFINED ENV{GITHUB_REF} AND NOT "$ENV{GITHUB_REF}" STREQUAL "")
    message(STATUS "github ref: $ENV{GITHUB_REF}")
    string(REGEX REPLACE "^refs/[^/]*/" "" GITHUB_BRANCH "$ENV{GITHUB_REF}")
    string(REGEX MATCH "refs/pull/([0-9]+)/merge" MATCHED_REF "$ENV{GITHUB_REF}")
    if (MATCHED_REF)
      set(PR_NUMBER "${CMAKE_MATCH_1}")
      set(GITHUB_BRANCH "")
      message(STATUS "PR number: ${PR_NUMBER}")
    else()
      set(PR_NUMBER "")
    endif()
  else()
    set(GITHUB_BRANCH "")
    set(PR_NUMBER "")
  endif()

  if (NOT "${PR_NUMBER}" STREQUAL "" AND NOT "${GITHUB_HEAD_REF}" STREQUAL "")
    set(GIT_BRANCH "pr-${PR_NUMBER}-${GITHUB_HEAD_REF}")
  elseif (NOT "${PR_NUMBER}" STREQUAL "" AND NOT "${GITHUB_BRANCH}" STREQUAL "")
    set(GIT_BRANCH "pr-${PR_NUMBER}-${GITHUB_BRANCH}")
  elseif (NOT "${PR_NUMBER}" STREQUAL "")
    set(GIT_BRANCH "pr-${PR_NUMBER}")
  elseif ("${PR_NUMBER}" STREQUAL "" AND NOT "${GITHUB_HEAD_REF}" STREQUAL "")
    set(GIT_BRANCH "${GITHUB_HEAD_REF}")
  elseif ("${PR_NUMBER}" STREQUAL "" AND NOT "${GITHUB_BRANCH}" STREQUAL "")
    set(GIT_BRANCH "${GITHUB_BRANCH}")
  elseif ("${PR_NUMBER}" STREQUAL "" AND NOT "${GITHUB_REF}" STREQUAL "")
    set(GIT_BRANCH "${GITHUB_REF}")
  elseif("${GIT_BRANCH}" STREQUAL "")
    message(STATUS "couldn't find branch")
    set(GIT_BRANCH "detached-head")
  endif()
else()
  # Extract remote name if the output contains a remote/branch format
  string(FIND "${GIT_REMOTE_NAME}" "/" INDEX)
  if (INDEX GREATER -1)
    string(SUBSTRING "${GIT_REMOTE_NAME}" 0 "${INDEX}" GIT_REMOTE_NAME)
  elseif("${GIT_REMOTE_NAME}" STREQUAL "")
    message(STATUS "reset to origin")
    set(GIT_REMOTE_NAME "origin")
  endif()
endif()

# Get remote link
message(STATUS "getting remote link")
execute_process(
  COMMAND git config --get remote.${GIT_REMOTE_NAME}.url
  OUTPUT_VARIABLE GIT_REMOTE_URL
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(APP_VERSION "${GIT_REV_LIST_NUMBER}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/common/scm_rev.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/src/common/scm_rev.cpp" @ONLY)
set_source_files_properties(src/shadps4.rc PROPERTIES COMPILE_DEFINITIONS "APP_VERSION=${APP_VERSION}")

message("-- end git things, remote: ${GIT_REMOTE_NAME}, branch: ${GIT_BRANCH}, link: ${GIT_REMOTE_URL}")

string(TOLOWER "${GIT_REMOTE_URL}" GIT_REMOTE_URL_LOWER)
if(NOT GIT_REMOTE_URL_LOWER MATCHES "shadps4-emu/shadps4-qtlauncher" OR NOT GIT_BRANCH STREQUAL "main")
    message(STATUS "not main, disabling auto update")
    set(ENABLE_UPDATER OFF)
endif()

if(WIN32 AND NOT CMAKE_PREFIX_PATH)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DetectQtInstallation.cmake")
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(fmt 10.2.0 CONFIG)
find_package(SDL3 3.1.2 CONFIG)
find_package(toml11 4.2.0 CONFIG)
find_package(VulkanHeaders 1.4.324 CONFIG)
find_package(pugixml 1.14 CONFIG)
list(POP_BACK CMAKE_MODULE_PATH)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    # libc++ requires -fexperimental-library to enable std::jthread and std::stop_token support.
    include(CheckCXXSymbolExists)
    check_cxx_symbol_exists(_LIBCPP_VERSION version LIBCPP)
    if(LIBCPP)
        add_compile_options(-fexperimental-library)
    endif()
endif()

add_subdirectory(externals)
include_directories(src)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent LinguistTools Network Multimedia)
qt_standard_project_setup()
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

set(QT_TRANSLATIONS "${PROJECT_SOURCE_DIR}/src/qt_gui/translations")
file(GLOB_RECURSE TRANSLATIONS_TS ${QT_TRANSLATIONS}/*.ts)

set_source_files_properties(${TRANSLATIONS_TS} PROPERTIES OUTPUT_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/translations")
qt_add_translation(TRANSLATIONS_QM ${TRANSLATIONS_TS})

set(TRANSLATIONS_QRC ${CMAKE_CURRENT_BINARY_DIR}/translations/translations.qrc)
file(WRITE ${TRANSLATIONS_QRC} "<RCC><qresource prefix=\"translations\">\n")
foreach (QM ${TRANSLATIONS_QM})
    get_filename_component(QM_FILE ${QM} NAME)
    file(APPEND ${TRANSLATIONS_QRC} "<file>${QM_FILE}</file>\n")
endforeach (QM)
file(APPEND ${TRANSLATIONS_QRC} "</qresource></RCC>")

qt_add_resources(TRANSLATIONS ${TRANSLATIONS_QRC})

set(COMMON src/common/logging/backend.cpp
           src/common/logging/backend.h
           src/common/logging/filter.cpp
           src/common/logging/filter.h
           src/common/logging/formatter.h
           src/common/logging/log_entry.h
           src/common/logging/log.h
           src/common/logging/text_formatter.cpp
           src/common/logging/text_formatter.h
           src/common/logging/types.h
           src/common/log_analyzer.cpp
           src/common/log_analyzer.h
           src/common/aes.h
           src/common/assert.cpp
           src/common/assert.h
           src/common/bounded_threadsafe_queue.h
           src/common/config.cpp
           src/common/config.h
           src/common/endian.h
           src/common/enum.h
           src/common/io_file.cpp
           src/common/io_file.h
           src/common/path_util.cpp
           src/common/path_util.h
           src/common/types.h
           src/common/memory_patcher.h
           src/common/memory_patcher.cpp
           ${CMAKE_CURRENT_BINARY_DIR}/src/common/scm_rev.cpp
           src/common/scm_rev.h
)

set(CORE src/core/file_format/elf.cpp
         src/core/file_format/elf.h
         src/core/file_format/psf.cpp
         src/core/file_format/psf.h
         src/core/file_format/trp.cpp
         src/core/file_format/trp.h
)

set(INPUT src/input/controller.cpp
          src/input/controller.h
)

qt_add_resources(RESOURCE_FILES src/shadps4.qrc)

if (ENABLE_UPDATER)
    set(UPDATER src/qt_gui/check_update.cpp
                src/qt_gui/check_update.h
    )
endif()

set(QT_GUI src/qt_gui/about_dialog.cpp
           src/qt_gui/about_dialog.h
           src/qt_gui/about_dialog.ui
           src/qt_gui/background_music_player.cpp
           src/qt_gui/background_music_player.h
           src/qt_gui/cheats_patches.cpp
           src/qt_gui/cheats_patches.h
           src/qt_gui/compatibility_info.cpp
           src/qt_gui/compatibility_info.h
           src/qt_gui/control_settings.cpp
           src/qt_gui/control_settings.h
           src/qt_gui/control_settings.ui
           src/qt_gui/kbm_gui.cpp	   
           src/qt_gui/kbm_gui.h
           src/qt_gui/kbm_gui.ui
           src/qt_gui/main_window_ui.h
           src/qt_gui/main_window.cpp
           src/qt_gui/main_window.h
           src/qt_gui/gui_context_menus.h
           src/qt_gui/game_list_utils.h
           src/qt_gui/game_info.cpp
           src/qt_gui/game_info.h
           src/qt_gui/game_list_frame.cpp
           src/qt_gui/game_list_frame.h
           src/qt_gui/game_grid_frame.cpp
           src/qt_gui/game_grid_frame.h
           src/qt_gui/game_install_dialog.cpp
           src/qt_gui/game_install_dialog.h
           src/qt_gui/trophy_viewer.cpp
           src/qt_gui/trophy_viewer.h
           src/qt_gui/elf_viewer.cpp
           src/qt_gui/elf_viewer.h
           src/qt_gui/kbm_config_dialog.cpp
           src/qt_gui/kbm_config_dialog.h
           src/qt_gui/kbm_help_dialog.cpp
           src/qt_gui/kbm_help_dialog.h
           src/qt_gui/main_window_themes.cpp
           src/qt_gui/main_window_themes.h
           src/qt_gui/log_presets_dialog.cpp
           src/qt_gui/log_presets_dialog.h
           src/qt_gui/settings_dialog.cpp
           src/qt_gui/settings_dialog.h
           src/qt_gui/settings_dialog.ui
           src/qt_gui/gui_settings.cpp
           src/qt_gui/gui_settings.h
           src/qt_gui/settings.cpp
           src/qt_gui/settings.h
           src/qt_gui/sdl_event_wrapper.cpp
           src/qt_gui/sdl_event_wrapper.h
           src/qt_gui/hotkeys.h
           src/qt_gui/hotkeys.cpp
           src/qt_gui/hotkeys.ui
           src/qt_gui/version_dialog.cpp
           src/qt_gui/version_dialog.h
           src/qt_gui/version_dialog.ui
           src/qt_gui/create_shortcut.cpp
           src/qt_gui/create_shortcut.h
           ${RESOURCE_FILES}
           ${TRANSLATIONS}
           ${UPDATER}
)

set(IPC src/ipc/ipc_client.cpp
        src/ipc/ipc_client.h
)
qt_add_executable(shadPS4QtLauncher
    ${INPUT}
    ${QT_GUI}
    ${COMMON}
    ${CORE}
    ${IPC}
    src/images/shadPS4.icns
    src/main.cpp
)

create_target_directory_groups(shadPS4QtLauncher)

target_link_libraries(shadPS4QtLauncher PRIVATE Vulkan::Headers fmt::fmt toml11::toml11 SDL3::SDL3 pugixml::pugixml volk_headers)

if (APPLE)
    # Include MoltenVK, along with an ICD file so it can be found by the system Vulkan loader if used for loading layers.
    set(MVK_BUNDLE_PATH "Resources/vulkan/icd.d")
    set_property(TARGET shadPS4QtLauncher APPEND PROPERTY BUILD_RPATH "@executable_path/../${MVK_BUNDLE_PATH}")
    set(MVK_DST ${CMAKE_CURRENT_BINARY_DIR}/shadPS4QtLauncher.app/Contents/${MVK_BUNDLE_PATH})

    add_custom_command(
        OUTPUT ${MVK_DST}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MVK_DST})

    set(MVK_DYLIB_SRC ${CMAKE_CURRENT_BINARY_DIR}/externals/MoltenVK/libMoltenVK.dylib)
    set(MVK_DYLIB_DST ${MVK_DST}/libMoltenVK.dylib)
    set(MVK_ICD_SRC ${CMAKE_CURRENT_SOURCE_DIR}/externals/MoltenVK/MoltenVK/MoltenVK/icd/MoltenVK_icd.json)
    set(MVK_ICD_DST ${MVK_DST}/MoltenVK_icd.json)

    add_custom_command(
        OUTPUT ${MVK_ICD_DST}
        DEPENDS ${MVK_ICD_SRC} ${MVK_DST}
        COMMAND ${CMAKE_COMMAND} -E copy ${MVK_ICD_SRC} ${MVK_ICD_DST})
    add_custom_command(
        OUTPUT ${MVK_DYLIB_DST}
        DEPENDS ${MVK_DYLIB_SRC} ${MVK_DST}
        COMMAND ${CMAKE_COMMAND} -E copy ${MVK_DYLIB_SRC} ${MVK_DYLIB_DST})
    add_custom_target(CopyMoltenVK DEPENDS ${MVK_ICD_DST} ${MVK_DYLIB_DST})
    add_dependencies(CopyMoltenVK MoltenVK)
    add_dependencies(shadPS4QtLauncher CopyMoltenVK)
endif()

target_link_libraries(shadPS4QtLauncher PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network Qt6::Multimedia)
if (ENABLE_UPDATER)
    add_definitions(-DENABLE_UPDATER)
endif()

if (WIN32)
    target_link_libraries(shadPS4QtLauncher PRIVATE mincore)

    if (MSVC)
        # MSVC likes putting opinions on what people can use, disable:
        add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS)
    endif()

    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)

    if (MSVC)
        # Needed for conflicts with time.h of windows.h
        add_definitions(-D_TIMESPEC_DEFINED)
    endif()

    # Target Windows 10 RS5
    add_definitions(-DNTDDI_VERSION=0x0A000006 -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00)

    if (MSVC)
        target_link_libraries(shadPS4QtLauncher PRIVATE clang_rt.builtins-x86_64.lib)
    endif()

    # Disable ASLR so we can reserve the user area
    if (MSVC)
        target_link_options(shadPS4QtLauncher PRIVATE /DYNAMICBASE:NO)
    else()
        target_link_options(shadPS4QtLauncher PRIVATE -Wl,--disable-dynamicbase)
    endif()

    # Increase stack commit area (Needed, otherwise there are crashes)
    if (MSVC)
        target_link_options(shadPS4QtLauncher PRIVATE /STACK:0x200000,0x200000)
    else()
        target_link_options(shadPS4QtLauncher PRIVATE -Wl,--stack,2097152)
    endif()
endif()

if (WIN32)
    target_sources(shadPS4QtLauncher PRIVATE src/shadps4.rc)
endif()

target_include_directories(shadPS4QtLauncher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# embed resources

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CMakeRC.cmake")
cmrc_add_resource_library(embedded-resources
        ALIAS res::embedded
        NAMESPACE res
	src/images/trophy.wav
        src/images/bronze.png
        src/images/gold.png
        src/images/platinum.png
        src/images/silver.png)
target_link_libraries(shadPS4QtLauncher PRIVATE res::embedded)

set_target_properties(shadPS4QtLauncher PROPERTIES
#       WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/dist/MacOSBundleInfo.plist.in"
    MACOSX_BUNDLE_ICON_FILE "shadPS4.icns"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${APP_VERSION}"
)

set_source_files_properties(src/images/shadPS4.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)

if (UNIX AND NOT APPLE)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(shadPS4QtLauncher PRIVATE ${OPENSSL_LIBRARIES})
endif()

# Install rules
install(TARGETS shadPS4QtLauncher BUNDLE DESTINATION .)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES "dist/net.shadps4.shadPS4.desktop" DESTINATION "share/applications")
    install(FILES "dist/net.shadps4.shadPS4.metainfo.xml" DESTINATION "share/metainfo")
    install(FILES ".github/shadps4.png" DESTINATION "share/icons/hicolor/512x512/apps" RENAME "net.shadps4.shadPS4.png")
    install(FILES "src/images/net.shadps4.shadPS4.svg" DESTINATION "share/icons/hicolor/scalable/apps")
endif()
