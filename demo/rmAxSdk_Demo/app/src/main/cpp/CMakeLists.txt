cmake_minimum_required(VERSION 3.22.1)
project(rmAxSdkJni LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(rmAxSdkJni SHARED rmAxSdkJni.cpp)

target_include_directories(rmAxSdkJni PRIVATE include)

# rmAxSdk1 预编译库（与 Qt6 依赖 .so 一起位于 jniLibs/${ANDROID_ABI}）
add_library(rmAxSdk1 SHARED IMPORTED)
set_target_properties(rmAxSdk1 PROPERTIES
    IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/librmAxSdk1.so)

# Qt6Core：SDK 的依赖，需直接链接以调用 QtAndroidPrivate::initJNI 缓存 JavaVM
add_library(Qt6Core SHARED IMPORTED)
set_target_properties(Qt6Core PROPERTIES
    IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libQt6Core_arm64-v8a.so)

find_library(lib_log log)
find_library(lib_android android)

# 使用 c++_shared 与 SDK 保持一致，避免跨边界传递 std::string 时堆不一致
target_link_libraries(rmAxSdkJni PRIVATE rmAxSdk1 Qt6Core ${lib_log} ${lib_android})
