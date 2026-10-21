# Builds the SDK's native library, apex_java: the JNI bridge (src/ApexJavaJni.c)
# with apex-sdk-core linked in statically.
#
# The core is in this repository only in compiled form (apex-sdk-core/: Apex.h
# and one static library per platform, from apex-sdk-core's java-kit CI job;
# tools/update_core.sh updates it). It is built with the core's "ANDROID"
# backend on every platform: despite the name, it sends requests through
# java.net.HttpURLConnection over JNI, so it works in any JVM and uses the
# JVM's certificates and proxy settings.
#
#   cmake -S native -B build/native-host -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/native-host --config Release
#
# -DAPEX_CORE_DIR=<apex-sdk-core checkout> builds the core from source instead,
# for testing an unreleased core.
cmake_minimum_required(VERSION 3.20)

# Static C runtime with MSVC, so the DLL needs no redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(apex_java LANGUAGES C CXX)

set(APEX_CORE_DIR "" CACHE PATH "Optional: an apex-sdk-core checkout to build the core from source")
if(APEX_CORE_DIR)
  if(NOT EXISTS "${APEX_CORE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "APEX_CORE_DIR (${APEX_CORE_DIR}) is not an apex-sdk-core checkout.")
  endif()
  set(APEX_SHARED OFF CACHE BOOL "" FORCE)
  set(APEX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(APEX_BUILD_SMOKE OFF CACHE BOOL "" FORCE)
  set(APEX_TRANSPORT ANDROID CACHE STRING "" FORCE)
  add_subdirectory(${APEX_CORE_DIR} apex-sdk-core)
  if(NOT ANDROID)
    # Cores before the java-kit job do not find jni.h themselves.
    find_package(JNI REQUIRED)
    target_include_directories(apex_core PRIVATE ${JNI_INCLUDE_DIRS})
  endif()
else()
  set(APEX_CORE_KIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/apex-sdk-core)
  if(ANDROID)
    set(APEX_CORE_PLATFORM android-${ANDROID_ABI})
  elseif(APPLE)
    set(APEX_CORE_PLATFORM macos)
  elseif(WIN32)
    set(APEX_CORE_PLATFORM windows-x86_64)
  else()
    set(APEX_CORE_PLATFORM linux-${CMAKE_SYSTEM_PROCESSOR})
  endif()
  if(MSVC)
    set(APEX_CORE_LIBRARY ${APEX_CORE_KIT_DIR}/lib/${APEX_CORE_PLATFORM}/apex_core.lib)
  else()
    set(APEX_CORE_LIBRARY ${APEX_CORE_KIT_DIR}/lib/${APEX_CORE_PLATFORM}/libapex_core.a)
  endif()
  if(NOT EXISTS ${APEX_CORE_LIBRARY})
    message(FATAL_ERROR "No compiled apex-sdk-core for ${APEX_CORE_PLATFORM} (${APEX_CORE_LIBRARY}).")
  endif()
  add_library(apex_core STATIC IMPORTED)
  set_target_properties(apex_core PROPERTIES
    IMPORTED_LOCATION ${APEX_CORE_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES ${APEX_CORE_KIT_DIR}/include)
  if(NOT WIN32 AND NOT ANDROID)
    find_package(Threads REQUIRED)
    set_property(TARGET apex_core APPEND PROPERTY INTERFACE_LINK_LIBRARIES Threads::Threads)
  endif()
endif()

add_library(apex_java SHARED src/ApexJavaJni.c)
target_link_libraries(apex_java PRIVATE apex_core)
set_target_properties(apex_java PROPERTIES LINKER_LANGUAGE CXX C_VISIBILITY_PRESET hidden)
if(WIN32)
  set_target_properties(apex_java PROPERTIES PREFIX "")  # apex_java.dll
endif()

if(NOT ANDROID)
  # The NDK has jni.h in its sysroot; desktops take it from the JDK.
  find_package(JNI REQUIRED)
  target_include_directories(apex_java PRIVATE ${JNI_INCLUDE_DIRS})
endif()

if(MSVC)
  target_compile_options(apex_java PRIVATE /W4)
else()
  target_compile_options(apex_java PRIVATE -std=c99 -Wall -Wextra -Wpedantic)
endif()

# Export JNI_OnLoad only.
if(APPLE)
  target_link_options(apex_java PRIVATE
    "-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/src/ApexJavaExports.macos.txt")
elseif(UNIX)
  target_link_options(apex_java PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/ApexJavaExports.map")
  set_property(TARGET apex_java APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/ApexJavaExports.map)
  if(NOT ANDROID)
    # No libstdc++ or libgcc_s version requirements on the user's machine.
    target_link_options(apex_java PRIVATE -static-libstdc++ -static-libgcc)
  endif()
endif()

if(ANDROID)
  # 16 KB pages, required for Android 15+ devices.
  target_link_options(apex_java PRIVATE "-Wl,-z,max-page-size=16384")
endif()
