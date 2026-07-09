cmake_minimum_required(VERSION 3.16.0)

# Pin minos so the binary's minimum macOS doesn't ride the build host's SDK
# (README + Info.plist claim 15.0; an unpinned build on a newer host silently
# raises it — #72). Must be set before project() to take effect.
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS version")
endif()

project(OniAll LANGUAGES C)

include(CMakeDependentOption)
cmake_dependent_option(ONI_BINK_VIDEO "Build with Bink Video support" OFF "WIN32;CMAKE_SIZEOF_VOID_P EQUAL 4" OFF)

# MSVC 19 finally
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-multichar -Wno-incompatible-pointer-types")
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-multichar -Wno-incompatible-pointer-types -Wno-incompatible-function-pointer-types")
	# #69 — a -Werror=incompatible-pointer-types fence was trialled 2026-07-09 to
	# make UUtUns32*-into-uintptr_t* mismatches (silent stack corruption on 64-bit)
	# a hard error. The tree was verified CLEAN of that class with the fence +
	# -ferror-limit=0, but ~139 pre-existing benign T**<->void** mismatches (same
	# diagnostic, no width change) block enabling it permanently. Tracked as a
	# follow-up: cast those sites, then land
	#   add_compile_options($<$<COMPILE_LANGUAGE:C>:-Werror=incompatible-pointer-types>)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(Platform_SDL TRUE)
else()
	set(Platform_SDL FALSE CACHE BOOL "Use SDL as platform layer")
endif()

include_directories(
        BungieFrameWork/BFW_Headers/
        BungieFrameWork/BFW_Source/BFW_Akira/
        BungieFrameWork/BFW_Source/BFW_Object/
        BungieFrameWork/BFW_Source/BFW_Batou/
        BungieFrameWork/BFW_Source/BFW_Bink/
        BungieFrameWork/BFW_Source/BFW_Bink/BinkHeaders/
        BungieFrameWork/BFW_Source/BFW_Console/
        BungieFrameWork/BFW_Source/BFW_DialogManager/
        BungieFrameWork/BFW_Source/BFW_DialogManager/DM_Cursor/
        BungieFrameWork/BFW_Source/BFW_DialogManager/VM_Views/
        BungieFrameWork/BFW_Source/BFW_FileManager/
        BungieFrameWork/BFW_Source/BFW_FileFormat/
        BungieFrameWork/BFW_Source/BFW_FileFormat/BMP/
        BungieFrameWork/BFW_Source/BFW_FileFormat/PSD/
        BungieFrameWork/BFW_Source/BFW_FileFormat/DDS/
        BungieFrameWork/BFW_Source/BFW_Group/
        BungieFrameWork/BFW_Source/BFW_Effect/
        BungieFrameWork/BFW_Source/BFW_LocalInput/
        BungieFrameWork/BFW_Source/BFW_MathLib/
        BungieFrameWork/BFW_Source/BFW_Motoko/
        BungieFrameWork/BFW_Source/BFW_Motoko/Engines/OGL_Common/
        BungieFrameWork/BFW_Source/BFW_Motoko/Engines/
        BungieFrameWork/BFW_Source/BFW_Motoko/Engines/DrawEngine/Direct3D/
        BungieFrameWork/BFW_Source/BFW_Motoko/Engines/DrawEngine/Software/
        BungieFrameWork/BFW_Source/BFW_Motoko/Engines/DrawEngine/3DFX_Glide/
        BungieFrameWork/BFW_Source/BFW_Motoko/Engines/GeomEngine/Software/
        BungieFrameWork/BFW_Source/BFW_Motoko/Engines/GeomEngine/GeomOpenGL/
        BungieFrameWork/BFW_Source/BFW_Motoko/Manager/
        BungieFrameWork/BFW_Source/BFW_Motoko/Manager/Win32/
        BungieFrameWork/BFW_Source/BFW_NetworkManager/
        BungieFrameWork/BFW_Source/BFW_NetworkManager/NM_Queues/
        BungieFrameWork/BFW_Source/BFW_NetworkManager/NM_WinSock/
        BungieFrameWork/BFW_Source/BFW_RedPig/
        BungieFrameWork/BFW_Source/BFW_RemoteInput/
        BungieFrameWork/BFW_Source/BFW_Shared/
        BungieFrameWork/BFW_Source/BFW_SoundSystem/
        BungieFrameWork/BFW_Source/BFW_SoundSystem2/
        BungieFrameWork/BFW_Source/BFW_TemplateManager/
        BungieFrameWork/BFW_Source/BFW_TextSystem/
        BungieFrameWork/BFW_Source/BFW_Totoro/
        BungieFrameWork/BFW_Source/BFW_Utility/
        BungieFrameWork/BFW_Source/BFW_WindowManager/
        BungieFrameWork/BFW_Source/BFW_WindowManager/WM_Cursor/
        BungieFrameWork/BFW_Source/BFW_WindowManager/WM_Dialog/
        BungieFrameWork/BFW_Source/BFW_WindowManager/WM_DrawContext/
        BungieFrameWork/BFW_Source/BFW_WindowManager/WM_PartSpecification/
        BungieFrameWork/BFW_Source/BFW_WindowManager/WM_Utilities/
        BungieFrameWork/BFW_Source/BFW_WindowManager/WM_Windows/
        BungieFrameWork/BFW_ToolSource/Common/LSSolutionFileIO/
        BungieFrameWork/BFW_ToolSource/Common/LSSolutionFileIO/LSReader
        BungieFrameWork/BFW_ToolSource/Common/TemplateExtractor
        BungieFrameWork/BFW_ToolSource/Win32/ModelExporter
        BungieFrameWork/BFW_Source/BFW_CommandLine/
        OniProj/OniGameSource/
        OniProj/OniGameSource/Oni_AI/
        OniProj/OniGameSource/Oni_Networking/
        OniProj/OniGameSource/Oni_Windows/
        OniProj/OniGameSource/Oni_Object/
        OniProj/OniGameSource/Oni_BinaryData/
        OniProj/OniGameSource/Oni_OutGameUI/
        BungieFrameWork/BFW_ToolSource/Common/Imp
        BungieFrameWork/BFW_Source/BFW_Motoko/Engines/DrawEngine/OpenGL/
)

if (Platform_SDL)
    find_package(OpenGL REQUIRED)
    find_package(OpenAL REQUIRED)
    find_package(SDL2 REQUIRED)
    include_directories(
        OniProj/OniGameSource/Platform_SDL/
        BungieFrameWork/BFW_Source/BFW_SoundSystem2/Platform_OpenAL/
        ${SDL2_INCLUDE_DIRS}
    )
elseif(WIN32)
    include_directories(
            BungieFrameWork/BFW_Source/BFW_LocalInput/Platform_Win32/
            BungieFrameWork/BFW_Source/BFW_SoundSystem/Platform_Win32/
            BungieFrameWork/BFW_Source/BFW_SoundSystem2/Platform_Win32/
            BungieFrameWork/BFW_Source/BFW_CommandLine/Platform_Win32/
            OniProj/OniGameSource/Platform_Win32/
    )
elseif (APPLE)
    include_directories(
            BungieFrameWork/BFW_Source/BFW_LocalInput/Platform_MacOS/
            BungieFrameWork/BFW_Source/BFW_SoundSystem/Platform_MacOS/
            BungieFrameWork/BFW_Source/BFW_SoundSystem2/Platform_MacOS/
            BungieFrameWork/BFW_Source/BFW_CommandLine/Platform_MacOS/
            OniProj/OniGameSource/Platform_MacOS/
    )
endif ()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Runtime output directory")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Library output directory")

# Configure-time build identity stamp (#46): "<branch>@<short-sha>[-dirty]".
# Baked into the session-start log banner (BFW_Error.c, via the compile
# definition set in OniProj's CMakeLists — same pattern as ONI_BUILD_EPOCH)
# and suffixed onto the dev bundle's version by the oni_app target, so
# coexisting dev builds are distinguishable in startup.txt and in Finder.
# Falls back to "unknown@unknown" when git (or the .git dir) is absent.
# Dirty check uses --untracked-files=no: only tracked modifications count,
# matching `git describe --dirty` semantics (stray untracked files would
# otherwise flag every build dirty).
set(ONI_BUILD_STAMP "unknown@unknown")
find_program(ONI_GIT_EXECUTABLE git)
if(ONI_GIT_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${ONI_GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE ONI_GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE ONI_GIT_BRANCH_RC
        ERROR_QUIET)
    execute_process(
        COMMAND ${ONI_GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE ONI_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE ONI_GIT_SHA_RC
        ERROR_QUIET)
    execute_process(
        COMMAND ${ONI_GIT_EXECUTABLE} status --porcelain --untracked-files=no
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE ONI_GIT_STATUS
        RESULT_VARIABLE ONI_GIT_STATUS_RC
        ERROR_QUIET)
    if(ONI_GIT_BRANCH_RC EQUAL 0 AND ONI_GIT_SHA_RC EQUAL 0
       AND NOT ONI_GIT_BRANCH STREQUAL "" AND NOT ONI_GIT_SHA STREQUAL "")
        set(ONI_GIT_DIRTY "")
        if(ONI_GIT_STATUS_RC EQUAL 0 AND NOT ONI_GIT_STATUS STREQUAL "")
            set(ONI_GIT_DIRTY "-dirty")
        endif()
        set(ONI_BUILD_STAMP "${ONI_GIT_BRANCH}@${ONI_GIT_SHA}${ONI_GIT_DIRTY}")
    endif()
    # Re-run configure when HEAD moves (commit / branch switch) so the stamp
    # doesn't silently go stale across rebuilds. Working-tree-only edits can
    # still leave the dirty flag stale until the next configure — acceptable
    # for a forensic hint, same staleness class as ONI_BUILD_EPOCH.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD"
        "${CMAKE_CURRENT_SOURCE_DIR}/.git/index")
endif()
message(STATUS "OniARM64 build stamp: ${ONI_BUILD_STAMP}")

# #72 — the release version (from Info.plist) joins the build stamp in the
# session-start log banner so a user log names the release it came from.
execute_process(
    COMMAND /usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" ${CMAKE_SOURCE_DIR}/macos/Info.plist
    OUTPUT_VARIABLE ONI_PLIST_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT ONI_PLIST_VERSION)
    set(ONI_PLIST_VERSION "unknown")
endif()
message(STATUS "OniARM64 version: ${ONI_PLIST_VERSION}")

if (ONI_BINK_VIDEO)
    add_subdirectory(OniProj/OniCMakeProjs/BinkProj/)
endif ()
add_subdirectory(OniProj/OniCMakeProjs/TEVCProj/)
add_subdirectory(OniProj/OniCMakeProjs/OniProj/)

# macOS .app bundle assembly
if(APPLE)
    include(macos/bundle.cmake)
endif()
