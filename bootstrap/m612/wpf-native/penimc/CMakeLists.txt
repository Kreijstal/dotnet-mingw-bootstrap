# CMake build for WPF's PenImc_cor3.dll with mingw-w64 (GCC or clang).
# Replaces dll/PenImc.vcxproj, tablib/TabLib.vcxproj and
# Shared/OSVersionHelper/OSVersionHelper.vcxproj of dotnet/wpf.
# Lives in src/Microsoft.DotNet.Wpf/src/PenImc/.
cmake_minimum_required(VERSION 3.20)
project(PenImc LANGUAGES C CXX RC)

set(WPF_VERSION_SUFFIX "_cor3" CACHE STRING "WPF native DLL suffix")
set(WPF_VERSION "8.0.0" CACHE STRING "WPF version for the version resource")
string(REPLACE "." ";" _wpfver "${WPF_VERSION}")
list(GET _wpfver 0 WPF_VER_MAJOR)
list(GET _wpfver 1 WPF_VER_MINOR)
list(GET _wpfver 2 WPF_VER_PATCH)
set(PENIMC_NAME "PenImc${WPF_VERSION_SUFFIX}")

set(WPF_SRC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(SHARED ${WPF_SRC}/Shared)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ATL REQUIRED IMPORTED_TARGET reactos-atl)
find_program(WIDL widl REQUIRED)

set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN})
string(CONCAT _wpfver_header
    "#define WPF_VER_MAJOR ${WPF_VER_MAJOR}\n#define WPF_VER_MINOR ${WPF_VER_MINOR}\n"
    "#define WPF_VER_PATCH ${WPF_VER_PATCH}\n#define WPF_VER_STR \"${WPF_VERSION}\"\n")
file(CONFIGURE OUTPUT ${GEN}/wpfver.h CONTENT "${_wpfver_header}" @ONLY)

# Same defines as eng/WpfArcadeSdk/tools/Wpf.Cpp.props (x64, release)
set(WPF_DEFS
    WIN32 _UNICODE UNICODE _WINDOWS STRICT
    _WIN32_WINNT=0x0A00 _WIN32_IE=0x0700 WINNT=1 WINVER=0x0A00
    SAL_NO_ATTRIBUTE_DECLARATIONS ARGITERATOR_SUPPORTED=0 NDEBUG)

# --- mingw-w64 header gaps -------------------------------------------------
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <windows.h>
#include <tpcshrd.h>
int main() { SYSTEM_EVENT_DATA d; PACKET_DESCRIPTION p; (void)d; (void)p; return 0; }"
    HAVE_COMPLETE_TPCSHRD)
set(MINGW_COMPAT_DEFS)
if(NOT HAVE_COMPLETE_TPCSHRD)
    list(APPEND MINGW_COMPAT_DEFS WPF_MINGW_TPCSHRD_INCOMPLETE)
endif()
set(MINGW_COMPAT_FLAGS "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/mingw/wpf_mingw_compat.h")

# --- MIDL replacement: widl ------------------------------------------------
set(IDL ${CMAKE_CURRENT_SOURCE_DIR}/dll/PenImc.idl)
add_custom_command(
    OUTPUT ${GEN}/PenImc.h ${GEN}/penimc_i.c ${GEN}/PenImc_p.c ${GEN}/dlldata.c ${GEN}/PenImc.tlb
    COMMAND ${WIDL} -m64 -Oif -I${CMAKE_CURRENT_SOURCE_DIR}/dll
            -h -H ${GEN}/PenImc.h
            -u -U ${GEN}/penimc_i.c
            -p -P ${GEN}/PenImc_p.c
            -t -T ${GEN}/PenImc.tlb
            ${IDL}
    COMMAND ${WIDL} --dlldata-only -o ${GEN}/dlldata.c PenImc
    DEPENDS ${IDL}
    COMMENT "widl PenImc.idl")
add_custom_target(penimc_idl DEPENDS ${GEN}/PenImc.h ${GEN}/penimc_i.c ${GEN}/PenImc_p.c ${GEN}/dlldata.c ${GEN}/PenImc.tlb)

# --- .def: preprocessed like GenerateModuleDefinitionFile.targets ----------
set(DEF_OUT ${GEN}/PenImc.def)
add_custom_command(
    OUTPUT ${DEF_OUT}
    COMMAND ${CMAKE_C_COMPILER} -E -P -x c -DDLL_NAME=${PENIMC_NAME}
            -I${SHARED}/inc ${CMAKE_CURRENT_SOURCE_DIR}/dll/PenImc.def -o ${DEF_OUT}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dll/PenImc.def)
add_custom_target(penimc_def DEPENDS ${DEF_OUT})

# --- OSVersionHelper (static) ---------------------------------------------
add_library(osversionhelper STATIC
    ${SHARED}/OSVersionHelper/osversionhelper.cpp
    ${SHARED}/OSVersionHelper/watsonreportinghelper.cpp)
target_compile_definitions(osversionhelper PRIVATE ${WPF_DEFS} ${MINGW_COMPAT_DEFS} _LIB)
target_compile_options(osversionhelper PRIVATE ${MINGW_COMPAT_FLAGS})
target_include_directories(osversionhelper PRIVATE mingw ${SHARED}/inc ${SHARED}/OSVersionHelper)

# --- TabLib (static) -------------------------------------------------------
add_library(tablib STATIC tablib/sidutils.cpp)
target_compile_definitions(tablib PRIVATE ${WPF_DEFS} ${MINGW_COMPAT_DEFS} _LIB)
target_compile_options(tablib PRIVATE ${MINGW_COMPAT_FLAGS})
target_include_directories(tablib PRIVATE mingw inc ${SHARED}/inc)

# --- PenImc_cor3.dll -------------------------------------------------------
add_library(penimc SHARED
    dll/dlldatax.c
    dll/ComApartmentVerifier.cpp
    dll/ComLockableWrapper.cpp
    dll/PenImc.cpp
    dll/PimcContext.cpp
    dll/PimcTablet.cpp
    dll/PimcManager.cpp
    dll/PimcSurrogate.cpp
    dll/SxSCOMRegistration.cpp
    dll/WispComLockExports.cpp
    dll/stdafx.cpp
    dll/PenImc.rc
    ${DEF_OUT})
add_dependencies(penimc penimc_idl penimc_def)
set_target_properties(penimc PROPERTIES OUTPUT_NAME ${PENIMC_NAME} PREFIX "")
target_compile_options(penimc PRIVATE
    "$<$<COMPILE_LANGUAGE:C,CXX>:${MINGW_COMPAT_FLAGS}>"
    "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/mingw/penimc_uuids.h>")
target_compile_definitions(penimc PRIVATE ${WPF_DEFS} ${MINGW_COMPAT_DEFS} _MERGE_PROXYSTUB _USRDLL _WINDLL)
target_include_directories(penimc PRIVATE
    mingw ${GEN} dll inc ${SHARED}/inc ${SHARED}/OSVersionHelper ${SHARED}/Tracing/native)
target_link_libraries(penimc PRIVATE PkgConfig::ATL tablib osversionhelper
    advapi32 uuid ole32 oleaut32 user32 shlwapi rpcrt4 shell32)
set_source_files_properties(dll/PenImc.rc PROPERTIES
    OBJECT_DEPENDS "${GEN}/PenImc.tlb"
    INCLUDE_DIRECTORIES "${GEN};${CMAKE_CURRENT_SOURCE_DIR}/dll;${CMAKE_CURRENT_SOURCE_DIR}/mingw")

install(TARGETS penimc RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
