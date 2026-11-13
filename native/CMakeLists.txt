cmake_minimum_required(VERSION 3.22.1)
project(avs3a_jni LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(AVS3A_BUILD_HOST_TESTS "Build host-only tests with an injected fake backend" OFF)
option(AVS3A_WITH_DECODER "Link the embedded reference decoder (full variant)" OFF)
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

# --- Core: framing, timeline, session, registry (no vendor/decoder knowledge).
add_library(avs3a_core STATIC
    src/header_parser.cpp src/framer.cpp src/timeline.cpp
    src/session.cpp src/registry.cpp)
set_target_properties(avs3a_core PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(avs3a_core PUBLIC include "${GENERATED_DIR}")
target_compile_options(avs3a_core PRIVATE -Wall -Wextra -Werror)

# --- Vendored av3adecoder reference implementation (explicit source list).
# CLI, WAV tooling and encoder sources are intentionally excluded.
set(AVS3A_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/av3adecoder")
set(AVS3A_DECODER_SOURCES
    "${AVS3A_VENDOR_DIR}/avs3Decoder/src/avs3_bitstream_dec.c"
    "${AVS3A_VENDOR_DIR}/avs3Decoder/src/avs3_dec.c"
    "${AVS3A_VENDOR_DIR}/avs3Decoder/src/avs3_hoa_dec.c"
    "${AVS3A_VENDOR_DIR}/avs3Decoder/src/avs3_init_dec.c"
    "${AVS3A_VENDOR_DIR}/avs3Decoder/src/avs3_mc_dec.c"
    "${AVS3A_VENDOR_DIR}/avs3Decoder/src/avs3_metadata_dec.c"
    "${AVS3A_VENDOR_DIR}/avs3Decoder/src/avs3_mix_dec.c"
    "${AVS3A_VENDOR_DIR}/avs3Decoder/src/avs3_mono_dec.c"
    "${AVS3A_VENDOR_DIR}/avs3Decoder/src/avs3_stereo_dec.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/activation_func.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/avs3_com.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/avs3_hoa_com.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/avs3_mc_com.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/avs3_rom_com.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/avs3_stereo_com.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/bwe_com.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/bwe_dec.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/cnn_layer.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/fd_spectrum_shaping_com.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/fd_spectrum_shaping_dec.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/fft.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/latent_quant.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/load_free_model.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/lsf_dec.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/mcr_com.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/mdct.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/mdct_dequant_decode.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/noise_filling.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/range_coder.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/tns.c"
    "${AVS3A_VENDOR_DIR}/libavs3_common/tools.c")

function(avs3a_add_decoder_target)
    add_library(avs3a_reference_decoder STATIC ${AVS3A_DECODER_SOURCES})
    set_target_properties(avs3a_reference_decoder PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden)
    target_include_directories(avs3a_reference_decoder PRIVATE
        "${AVS3A_VENDOR_DIR}/avs3Decoder/include"
        "${AVS3A_VENDOR_DIR}/libavs3_common"
        "${AVS3A_VENDOR_DIR}/libavs3_debug")
    target_compile_options(avs3a_reference_decoder PRIVATE
        -std=c99 -fsigned-char -fPIC -O3)
    target_compile_definitions(avs3a_reference_decoder PRIVATE AVS3A_DECODER_ONLY=1)
    if(ANDROID)
        if(ANDROID_ABI STREQUAL "arm64-v8a")
            target_compile_definitions(avs3a_reference_decoder PRIVATE ARCH_AARCH64)
        endif()
    endif()
endfunction()

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

    set(AVS3A_JNI_SOURCES
        src/jni_bridge.cpp src/build_info.cpp decoder/source_backend.cpp)

    if(AVS3A_WITH_DECODER)
        avs3a_add_decoder_target()
        list(APPEND AVS3A_JNI_SOURCES decoder/decoder_adapter.c)
    endif()

    add_library(avs3a_jni SHARED ${AVS3A_JNI_SOURCES})
    target_include_directories(avs3a_jni PRIVATE decoder)
    if(AVS3A_WITH_DECODER)
        target_include_directories(avs3a_jni PRIVATE
            "${AVS3A_VENDOR_DIR}/avs3Decoder/include"
            "${AVS3A_VENDOR_DIR}/libavs3_common")
        target_link_libraries(avs3a_jni PRIVATE avs3a_reference_decoder)
        target_compile_definitions(avs3a_jni PRIVATE AVS3A_WITH_DECODER=1)
    endif()
    target_link_libraries(avs3a_jni PRIVATE avs3a_core log m)
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
        tests/timeline_test.cpp tests/registry_test.cpp
        tests/support/fake_backend.cpp)
    target_link_libraries(avs3a_core_tests PRIVATE avs3a_core Threads::Threads)
    target_include_directories(avs3a_core_tests PRIVATE tests/support)
    add_test(NAME avs3a_core_tests COMMAND avs3a_core_tests)

    # Exercise the actual registered JNI callbacks, not only Session::close().
    # This executable is host-test-only and never enters either SDK AAR.
    find_package(JNI REQUIRED)
    add_executable(avs3a_jni_release_tests src/jni_bridge.cpp tests/jni_release_test.cpp)
    target_include_directories(avs3a_jni_release_tests PRIVATE ${JNI_INCLUDE_DIRS})
    target_compile_definitions(avs3a_jni_release_tests PRIVATE AVS3A_HOST_TEST_FACTORY=1)
    target_link_libraries(avs3a_jni_release_tests PRIVATE avs3a_core Threads::Threads)
    target_compile_options(avs3a_jni_release_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME avs3a_jni_decoder_release COMMAND avs3a_jni_release_tests decoder)
    add_test(NAME avs3a_jni_parser_release COMMAND avs3a_jni_release_tests parser)
    # A release regression must fail on a deadline instead of wedging the test runner.
    set_tests_properties(avs3a_jni_decoder_release avs3a_jni_parser_release PROPERTIES TIMEOUT 10)
else()
    message(FATAL_ERROR "Desktop configure must explicitly select host tests")
endif()
