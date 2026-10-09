#include "avs3a/backend.h"
#include "avs3a/status.h"
#include "avs3a/abi_access.h"
#include <dlfcn.h>
#include <cstring>
#include <string>
#include <memory>

namespace avs3a {

typedef void* (*Avs3AllocFn)(void);
typedef void (*Avs3InitFn)(void*, const char*);
typedef void (*Avs3DecodeFn)(void*, int16_t*);
typedef void (*Avs3DestroyFn)(void*);
typedef void (*Avs3ResetFn)(void);

struct VendorApi {
    Avs3AllocFn alloc;
    Avs3InitFn init;
    Avs3DecodeFn decode;
    Avs3DestroyFn destroy;
    Avs3ResetFn reset_bitstream;
};

struct VendorLoaderResult {
    void* lib_handle;
    VendorApi api;
    std::string soname;
    std::string load_path;
    bool verified;
};

class VendorLoaderAndroid {
public:
    VendorLoaderAndroid() = default;
    ~VendorLoaderAndroid() = default;

    VendorLoaderResult open_verified_library(const char* path) {
        VendorLoaderResult result = {nullptr, {nullptr,nullptr,nullptr,nullptr,nullptr}, "", "", false};
        if (!path) return result;

        void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!handle) return result;

        result.lib_handle = handle;
        result.load_path = path;

        result.api.alloc = reinterpret_cast<Avs3AllocFn>(dlsym(handle, "Avs3AllocDecoder"));
        result.api.init = reinterpret_cast<Avs3InitFn>(dlsym(handle, "Avs3InitDecoder"));
        result.api.decode = reinterpret_cast<Avs3DecodeFn>(dlsym(handle, "Avs3Decode"));
        result.api.destroy = reinterpret_cast<Avs3DestroyFn>(dlsym(handle, "Avs3DecoderDestroy"));
        result.api.reset_bitstream = reinterpret_cast<Avs3ResetFn>(dlsym(handle, "ResetBitstream"));

        if (!result.api.alloc || !result.api.init || !result.api.decode ||
            !result.api.destroy || !result.api.reset_bitstream) {
            return result;
        }

        result.verified = true;
        result.soname = "libavs3a_decoder.so";
        return result;
    }

    void close_library(void* handle) {
        if (handle) dlclose(handle);
    }
};

std::unique_ptr<DecoderBackend> create_vendor_backend(int process_abi, void* lib_handle, const VendorApi& api);

} // namespace avs3a
