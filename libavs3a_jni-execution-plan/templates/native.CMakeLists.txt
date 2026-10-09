cmake_minimum_required(VERSION 3.22.1)
project(avs3a_jni LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(AVS3A_BUILD_HOST_TESTS "Build host-only tests with an injected fake backend" OFF)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/abi-contract.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/frame-dialect.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/../protocol/jni-contract.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/../ci/generate-contracts.py")
if(ANDROID)
    set(CONTRACT_ABI "${ANDROID_ABI}")
else()
    set(CONTRACT_ABI "host-test")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../ci/generate-contracts.py"
        --abi "${CONTRACT_ABI}"
        --abi-contract "${CMAKE_CURRENT_SOURCE_DIR}/vendor/abi-contract.json"
        --dialect-contract "${CMAKE_CURRENT_SOURCE_DIR}/vendor/frame-dialect.json"
        --output "${GENERATED_DIR}"
    RESULT_VARIABLE CONTRACT_RESULT)
if(NOT CONTRACT_RESULT EQUAL 0)
    message(FATAL_ERROR "Contract generation failed")
endif()

add_library(avs3a_core STATIC
    src/header_parser.cpp src/framer.cpp src/timeline.cpp
    src/session.cpp src/registry.cpp src/abi_access.cpp)
set_target_properties(avs3a_core PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(avs3a_core PUBLIC include "${GENERATED_DIR}")
target_compile_options(avs3a_core PRIVATE -Wall -Wextra -Werror)

if(ANDROID)
    if(AVS3A_BUILD_HOST_TESTS)
        message(FATAL_ERROR "Fake-backed host tests may not be included in Android SDK builds")
    endif()
    if(NOT ANDROID_ABI STREQUAL "arm64-v8a" AND NOT ANDROID_ABI STREQUAL "armeabi-v7a")
        message(FATAL_ERROR "Unsupported ABI")
    endif()
    if(NOT DEFINED AVS3A_SDK_VERSION OR NOT DEFINED AVS3A_SOURCE_COMMIT)
        message(FATAL_ERROR "SDK build identity missing")
    endif()
    add_library(avs3a_jni SHARED
        src/jni_bridge.cpp src/build_info.cpp src/vendor_loader_android.cpp src/vendor_backend.cpp)
    target_link_libraries(avs3a_jni PRIVATE avs3a_core log dl m)
    target_compile_definitions(avs3a_jni PRIVATE
        AVS3A_SDK_VERSION="${AVS3A_SDK_VERSION}"
        AVS3A_SOURCE_COMMIT="${AVS3A_SOURCE_COMMIT}")
    target_compile_options(avs3a_jni PRIVATE -Wall -Wextra -Werror -g)
    set_target_properties(avs3a_jni PROPERTIES
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    target_link_options(avs3a_jni PRIVATE "-Wl,--no-undefined" "-Wl,--build-id=sha1"
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/avs3a_jni.map")
    set_property(TARGET avs3a_jni APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/avs3a_jni.map")
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        target_link_options(avs3a_jni PRIVATE "-Wl,-z,max-page-size=16384")
    endif()
    if(DEFINED AVS3A_UNSTRIPPED_DIR)
        add_custom_command(TARGET avs3a_jni POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${AVS3A_UNSTRIPPED_DIR}/${CMAKE_BUILD_TYPE}/${ANDROID_ABI}"
            COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:avs3a_jni>"
                "${AVS3A_UNSTRIPPED_DIR}/${CMAKE_BUILD_TYPE}/${ANDROID_ABI}/libavs3a_jni.so"
            VERBATIM)
    endif()
elseif(AVS3A_BUILD_HOST_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(avs3a_core_tests
        tests/test_main.cpp tests/framer_test.cpp tests/session_test.cpp
        tests/timeline_test.cpp tests/registry_test.cpp tests/abi_access_test.cpp
        tests/support/fake_backend.cpp)
    target_link_libraries(avs3a_core_tests PRIVATE avs3a_core Threads::Threads)
    add_test(NAME avs3a_core_tests COMMAND avs3a_core_tests)
else()
    message(FATAL_ERROR "Desktop configure must explicitly select host tests")
endif()
