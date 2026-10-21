# Builds the SDK's native library, apex_java: the JNI bridge (src/ApexJavaJni.c)
# with apex-sdk-core linked in statically. The core's sources are not in this
# repository: APEX_CORE_DIR names an apex-sdk-core checkout or source release.
#
# The core is built with its "ANDROID" backend on every platform: despite the
# name, it sends requests through java.net.HttpURLConnection over JNI, so it
# works in any JVM and uses the JVM's certificates and proxy settings.
#
#   cmake -S native -B build/native-host -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/native-host --config Release
#
# tools/build_natives.sh does this and copies the library to where the JAR
# picks it up.
cmake_minimum_required(VERSION 3.20)

# Static C runtime with MSVC, so the DLL needs no redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(apex_java LANGUAGES C CXX)

set(APEX_SHARED OFF CACHE BOOL "" FORCE)
set(APEX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(APEX_BUILD_SMOKE OFF CACHE BOOL "" FORCE)
set(APEX_TRANSPORT ANDROID CACHE STRING "" FORCE)
set(APEX_CORE_DIR "" CACHE PATH "An apex-sdk-core checkout or source release")
if(NOT EXISTS "${APEX_CORE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Set APEX_CORE_DIR to an apex-sdk-core checkout or source release "
    "(for example -DAPEX_CORE_DIR=../apex-sdk-core).")
endif()
add_subdirectory(${APEX_CORE_DIR} apex-sdk-core)

add_library(apex_java SHARED src/ApexJavaJni.c)
target_link_libraries(apex_java PRIVATE apex_core)
set_target_properties(apex_java PROPERTIES LINKER_LANGUAGE CXX C_VISIBILITY_PRESET hidden)
if(WIN32)
  set_target_properties(apex_java PROPERTIES PREFIX "")  # apex_java.dll
endif()

if(NOT ANDROID)
  # The NDK has jni.h in its sysroot; desktops take it from the JDK.
  find_package(JNI REQUIRED)
  target_include_directories(apex_core PRIVATE ${JNI_INCLUDE_DIRS})
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
