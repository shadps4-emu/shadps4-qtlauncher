# SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
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

project(shadPS4 CXX C ASM ${ADDITIONAL_LANGUAGES})

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

option(ENABLE_QT_GUI "Enable the Qt GUI. If not selected then the emulator uses a minimal SDL-based UI instead" ON)
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
    # Target the same CPU architecture as the PS4, to maintain the same level of compatibility.
    # Exclude SSE4a as it is only available on AMD CPUs.
    add_compile_options(-march=btver2 -mtune=generic -mno-sse4a)
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
  message("check default push")
  execute_process(
    COMMAND git config --get remote.pushDefault
    OUTPUT_VARIABLE GIT_REMOTE_NAME
    RESULT_VARIABLE GIT_REMOTE_RESULT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif()

# If running in GitHub Actions and the above fails
if (GIT_REMOTE_RESULT OR GIT_REMOTE_NAME STREQUAL "")
  message("check github")
  set(GIT_REMOTE_NAME "origin")

  # Retrieve environment variables
  if (DEFINED ENV{GITHUB_HEAD_REF} AND NOT "$ENV{GITHUB_HEAD_REF}" STREQUAL "")
    message("github head ref: $ENV{GITHUB_HEAD_REF}")
    set(GITHUB_HEAD_REF "$ENV{GITHUB_HEAD_REF}")
  else()
    set(GITHUB_HEAD_REF "")
  endif()

  if (DEFINED ENV{GITHUB_REF} AND NOT "$ENV{GITHUB_REF}" STREQUAL "")
    message("github ref: $ENV{GITHUB_REF}")
    string(REGEX REPLACE "^refs/[^/]*/" "" GITHUB_BRANCH "$ENV{GITHUB_REF}")
    string(REGEX MATCH "refs/pull/([0-9]+)/merge" MATCHED_REF "$ENV{GITHUB_REF}")
    if (MATCHED_REF)
      set(PR_NUMBER "${CMAKE_MATCH_1}")
      set(GITHUB_BRANCH "")
      message("PR number: ${PR_NUMBER}")
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
  else()
    message("couldn't find branch")
    set(GIT_BRANCH "detached-head")
  endif()
else()
  # Extract remote name if the output contains a remote/branch format
  string(FIND "${GIT_REMOTE_NAME}" "/" INDEX)
  if (INDEX GREATER -1)
    string(SUBSTRING "${GIT_REMOTE_NAME}" 0 "${INDEX}" GIT_REMOTE_NAME)
  else()
    # If no remote is present (only a branch name), default to origin
    set(GIT_REMOTE_NAME "origin")
  endif()
endif()

# Get remote link
message("getting remote link")
execute_process(
  COMMAND git config --get remote.${GIT_REMOTE_NAME}.url
  OUTPUT_VARIABLE GIT_REMOTE_URL
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Set Version
set(EMULATOR_VERSION_MAJOR "0")
set(EMULATOR_VERSION_MINOR "8")
set(EMULATOR_VERSION_PATCH "1")

set_source_files_properties(src/shadps4.rc PROPERTIES COMPILE_DEFINITIONS "EMULATOR_VERSION_MAJOR=${EMULATOR_VERSION_MAJOR};EMULATOR_VERSION_MINOR=${EMULATOR_VERSION_MINOR};EMULATOR_VERSION_PATCH=${EMULATOR_VERSION_PATCH}")

set(APP_VERSION "${EMULATOR_VERSION_MAJOR}.${EMULATOR_VERSION_MINOR}.${EMULATOR_VERSION_PATCH} WIP")
set(APP_IS_RELEASE false)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/common/scm_rev.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/src/common/scm_rev.cpp" @ONLY)

message("end git things, remote: ${GIT_REMOTE_NAME}, branch: ${GIT_BRANCH}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Boost 1.84.0 CONFIG)
find_package(fmt 10.2.0 CONFIG)
find_package(glslang 15 CONFIG)
find_package(half 1.12.0 MODULE)
find_package(magic_enum 0.9.7 CONFIG)
find_package(SDL3 3.1.2 CONFIG)
find_package(stb MODULE)
find_package(toml11 4.2.0 CONFIG)
find_package(tsl-robin-map 1.3.0 CONFIG)
find_package(VulkanHeaders 1.4.309 CONFIG)
find_package(xbyak 7.07 CONFIG)
find_package(xxHash 0.8.2 MODULE)
find_package(ZLIB 1.3 MODULE)
find_package(Zydis 5.0.0 CONFIG)
find_package(pugixml 1.14 CONFIG)
if (APPLE)
    find_package(date 3.0.1 CONFIG)
endif()
list(POP_BACK CMAKE_MODULE_PATH)

# Note: Windows always has these functions through winpthreads
include(CheckSymbolExists)
check_symbol_exists(pthread_mutex_timedlock "pthread.h" HAVE_PTHREAD_MUTEX_TIMEDLOCK)
if(HAVE_PTHREAD_MUTEX_TIMEDLOCK OR WIN32)
    add_compile_options(-DHAVE_PTHREAD_MUTEX_TIMEDLOCK)
endif()

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
include_directories(Resources)

if(ENABLE_QT_GUI)
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
endif()

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
           src/common/aes.h
           src/common/alignment.h
           src/common/arch.h
           src/common/assert.cpp
           src/common/assert.h
           src/common/bit_field.h
           src/common/bounded_threadsafe_queue.h
           src/common/concepts.h
           src/common/config.cpp
           src/common/config.h
           src/common/cstring.h
           src/common/elf_info.h
           src/common/endian.h
           src/common/enum.h
           src/common/io_file.cpp
           src/common/io_file.h
           src/common/error.cpp
           src/common/error.h
           src/common/scope_exit.h
           src/common/fixed_value.h
           src/common/func_traits.h
           src/common/native_clock.cpp
           src/common/native_clock.h
           src/common/path_util.cpp
           src/common/path_util.h
           src/common/object_pool.h
           src/common/polyfill_thread.h
           src/common/rdtsc.cpp
           src/common/rdtsc.h
           src/common/sha1.h
           src/common/signal_context.h
           src/common/signal_context.cpp
           src/common/singleton.h
           src/common/slab_heap.h
           src/common/slot_vector.h
           src/common/spin_lock.cpp
           src/common/spin_lock.h
           src/common/stb.cpp
           src/common/stb.h
           src/common/string_literal.h
           src/common/string_util.cpp
           src/common/string_util.h
           src/common/thread.cpp
           src/common/thread.h
           src/common/types.h
           src/common/uint128.h
           src/common/unique_function.h
           src/common/va_ctx.h
           src/common/ntapi.h
           src/common/ntapi.cpp
           src/common/number_utils.h
           src/common/number_utils.cpp
           src/common/memory_patcher.h
           src/common/memory_patcher.cpp
           ${CMAKE_CURRENT_BINARY_DIR}/src/common/scm_rev.cpp
           src/common/scm_rev.h
)

set(CORE src/core/file_format/pfs.h
         src/core/file_format/psf.cpp
         src/core/file_format/psf.h
         src/core/file_format/trp.cpp
         src/core/file_format/trp.h
)

set(EMULATOR src/emulator.cpp
             src/emulator.h
)

# The above is shared in SDL and Qt version (TODO share them all)

if(ENABLE_QT_GUI)
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
           src/qt_gui/settings_dialog.cpp
           src/qt_gui/settings_dialog.h
           src/qt_gui/settings_dialog.ui
           src/qt_gui/main.cpp
           ${EMULATOR}
           ${RESOURCE_FILES}
           ${TRANSLATIONS}
           ${UPDATER}
)
endif()

if (ENABLE_QT_GUI)
    qt_add_executable(shadps4
        ${AUDIO_CORE}
        ${INPUT}
        ${QT_GUI}
        ${COMMON}
        ${CORE}
        ${EMULATOR}
        src/images/shadPS4.icns
    )
else()
    add_executable(shadps4
        ${AUDIO_CORE}
        ${INPUT}
        ${COMMON}
        ${CORE}
        ${EMULATOR}
        src/main.cpp
        src/emulator.cpp
        src/emulator.h
        src/sdl_window.h
        src/sdl_window.cpp
    )
endif()

create_target_directory_groups(shadps4)

target_link_libraries(shadps4 PRIVATE magic_enum::magic_enum fmt::fmt toml11::toml11 tsl::robin_map half::half ZLIB::ZLIB Vulkan::Headers volk_headers)
target_link_libraries(shadps4 PRIVATE Boost::headers SDL3::SDL3 pugixml::pugixml stb::headers)


# Optional due to https://github.com/shadps4-emu/shadPS4/issues/1704
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux" AND ENABLE_USERFAULTFD)
    target_compile_definitions(shadps4 PRIVATE ENABLE_USERFAULTFD)
endif()

if (APPLE)
    # Include MoltenVK, along with an ICD file so it can be found by the system Vulkan loader if used for loading layers.
    if (ENABLE_QT_GUI)
        set(MVK_BUNDLE_PATH "Resources/vulkan/icd.d")
        set_property(TARGET shadps4 APPEND PROPERTY BUILD_RPATH "@executable_path/../${MVK_BUNDLE_PATH}")
        set(MVK_DST ${CMAKE_CURRENT_BINARY_DIR}/shadps4.app/Contents/${MVK_BUNDLE_PATH})
    else()
        set_property(TARGET shadps4 APPEND PROPERTY BUILD_RPATH "@executable_path")
        set(MVK_DST ${CMAKE_CURRENT_BINARY_DIR})
    endif()

    set(MVK_DYLIB_SRC ${CMAKE_CURRENT_BINARY_DIR}/externals/MoltenVK/libMoltenVK.dylib)
    set(MVK_DYLIB_DST ${MVK_DST}/libMoltenVK.dylib)
    set(MVK_ICD_SRC ${CMAKE_CURRENT_SOURCE_DIR}/externals/MoltenVK/MoltenVK/MoltenVK/icd/MoltenVK_icd.json)
    set(MVK_ICD_DST ${MVK_DST}/MoltenVK_icd.json)

    add_custom_command(
        OUTPUT ${MVK_DST}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MVK_DST})
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
    add_dependencies(shadps4 CopyMoltenVK)

    if (ARCHITECTURE STREQUAL "x86_64")
        # Reserve system-managed memory space.
        target_link_options(shadps4 PRIVATE -Wl,-no_pie,-no_fixup_chains,-no_huge,-pagezero_size,0x4000,-segaddr,TCB_SPACE,0x4000,-segaddr,SYSTEM_MANAGED,0x400000,-segaddr,SYSTEM_RESERVED,0x7FFFFC000,-image_base,0x20000000000)
    endif()

endif()

if (NOT ENABLE_QT_GUI)
  target_link_libraries(shadps4 PRIVATE SDL3::SDL3)
endif()

if (ENABLE_QT_GUI)
    target_link_libraries(shadps4 PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network Qt6::Multimedia)
    add_definitions(-DENABLE_QT_GUI)
    if (ENABLE_UPDATER)
        add_definitions(-DENABLE_UPDATER)
    endif()
endif()

if (WIN32)
    target_link_libraries(shadps4 PRIVATE mincore)

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
        target_link_libraries(shadps4 PRIVATE clang_rt.builtins-x86_64.lib)
    endif()

    # Disable ASLR so we can reserve the user area
    if (MSVC)
        target_link_options(shadps4 PRIVATE /DYNAMICBASE:NO)
    else()
        target_link_options(shadps4 PRIVATE -Wl,--disable-dynamicbase)
    endif()

    # Increase stack commit area (Needed, otherwise there are crashes)
    if (MSVC)
        target_link_options(shadps4 PRIVATE /STACK:0x200000,0x200000)
    else()
        target_link_options(shadps4 PRIVATE -Wl,--stack,2097152)
    endif()
endif()

if (WIN32)
    target_sources(shadps4 PRIVATE src/shadps4.rc)
endif()

add_definitions(-DBOOST_ASIO_STANDALONE)

target_include_directories(shadps4 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})


# embed resources

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CMakeRC.cmake")
cmrc_add_resource_library(embedded-resources
        ALIAS res::embedded
        NAMESPACE res
        src/images/bronze.png
        src/images/gold.png
        src/images/platinum.png
        src/images/silver.png)
target_link_libraries(shadps4 PRIVATE res::embedded)

if (ENABLE_QT_GUI)
    set_target_properties(shadps4 PROPERTIES
#       WIN32_EXECUTABLE ON
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/dist/MacOSBundleInfo.plist.in"
        MACOSX_BUNDLE_ICON_FILE "shadPS4.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${APP_VERSION}"
    )

    set_source_files_properties(src/images/shadPS4.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
endif()

if (UNIX AND NOT APPLE)
    if (ENABLE_QT_GUI)
        find_package(OpenSSL REQUIRED)
        target_link_libraries(shadps4 PRIVATE ${OPENSSL_LIBRARIES})
    endif()
endif()


# Install rules
install(TARGETS shadps4 BUNDLE DESTINATION .)

if (ENABLE_QT_GUI AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES "dist/net.shadps4.shadPS4.desktop" DESTINATION "share/applications")
    install(FILES "dist/net.shadps4.shadPS4.metainfo.xml" DESTINATION "share/metainfo")
    install(FILES ".github/shadps4.png" DESTINATION "share/icons/hicolor/512x512/apps" RENAME "net.shadps4.shadPS4.png")
    install(FILES "src/images/net.shadps4.shadPS4.svg" DESTINATION "share/icons/hicolor/scalable/apps")
endif()
