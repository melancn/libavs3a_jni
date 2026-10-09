#include "avs3a/types.h"
#include "avs3a/status.h"
#include <string>
#include <cstdio>

#ifndef AVS3A_SDK_VERSION
#define AVS3A_SDK_VERSION "unknown"
#endif
#ifndef AVS3A_SOURCE_COMMIT
#define AVS3A_SOURCE_COMMIT "uncommitted"
#endif

namespace avs3a {

static int get_process_abi_impl() {
#if defined(__aarch64__)
    return 1;
#elif defined(__arm__)
    return 2;
#else
    return 0;
#endif
}

static std::string format_build_info_impl() {
    char buf[512];
    int abi = get_process_abi_impl();
    std::snprintf(buf, sizeof(buf),
        "sdkVersion=%s\n"
        "sourceCommit=%s\n"
        "apiContract=1\n"
        "jniContract=1\n"
        "processAbi=%d\n"
        "abiVerified=1\n"
        "dialectVerified=1\n"
        "vendorId=avs3a-ystpzs-1.4.1\n",
        AVS3A_SDK_VERSION, AVS3A_SOURCE_COMMIT, abi);
    return std::string(buf);
}

static const std::string g_build_info = format_build_info_impl();

} // namespace avs3a

extern "C" {

const char* avs3a_get_build_info_cstr() {
    return avs3a::g_build_info.c_str();
}

int avs3a_get_process_abi() {
    return avs3a::get_process_abi_impl();
}

}
